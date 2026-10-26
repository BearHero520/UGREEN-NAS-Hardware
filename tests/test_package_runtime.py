"""ELF runtime manifest tests. Synthetic objects are parsed, never executed."""
from pathlib import Path
import importlib.util
import struct
import unittest

spec = importlib.util.spec_from_file_location("almighty_packager", Path(__file__).resolve().parents[1] / "scripts/package-almighty.py")
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


def fixture(machine=62, interpreter=b"/lib64/ld-linux-x86-64.so.2\0"):
    # Sectionless ELF64: PT_LOAD, PT_INTERP and PT_DYNAMIC. All addresses
    # deliberately differ from offsets, as in a normal non-PIE executable.
    data = bytearray(1024)
    data[:7] = b"\x7fELF\x02\x01\x01"
    struct.pack_into("<HHI", data, 16, 2, machine, 1)
    struct.pack_into("<Q", data, 32, 64)
    struct.pack_into("<HHH", data, 52, 64, 56, 3)
    struct.pack_into("<IIQQQQQQ", data, 64, 1, 5, 0, 0x400000, 0, len(data), len(data), 4096)
    struct.pack_into("<IIQQQQQQ", data, 120, 3, 4, 256, 0x400100, 0, len(interpreter), len(interpreter), 1)
    dynamic = [(1, 1), (5, 0x400200), (10, 128), (0x6ffffffe, 0x400300), (0x6fffffff, 1), (0, 0)]
    struct.pack_into("<IIQQQQQQ", data, 176, 2, 6, 320, 0x400140, 0, len(dynamic)*16, len(dynamic)*16, 8)
    data[256:256+len(interpreter)] = interpreter
    for index, pair in enumerate(dynamic):
        struct.pack_into("<qQ", data, 320+16*index, *pair)
    strings = b"\0libc.so.6\0GLIBC_2.9\0GLIBC_2.27\0GLIBC_ABI_DT_RELR\0"
    data[512:512+len(strings)] = strings
    struct.pack_into("<HHIII", data, 768, 1, 2, 1, 16, 0)
    struct.pack_into("<IHHII", data, 784, 0, 0, 2, strings.index(b"GLIBC_2.9"), 16)
    struct.pack_into("<IHHII", data, 800, 0, 0, 3, strings.index(b"GLIBC_2.27"), 0)
    return data


class RuntimeManifestTests(unittest.TestCase):
    def parse(self, data):
        return packager.elf_runtime(bytes(data), "synthetic ELF")

    def rejects(self, data, message):
        with self.assertRaisesRegex(packager.PackageError, message):
            self.parse(data)

    def test_sectionless_objects_and_numeric_version_order(self):
        for machine, interpreter in [(62, b"/lib64/ld-linux-x86-64.so.2\0"), (183, b"/lib/ld-linux-aarch64.so.1\0")]:
            result = self.parse(fixture(machine, interpreter))
            self.assertEqual(result["interpreter"], interpreter[:-1].decode("ascii"))
            self.assertEqual(result["needed_libraries"], ["libc.so.6"])
            self.assertEqual(result["glibc_symbol_floor"], "2.27")
            self.assertEqual(result["required_symbol_versions"], {"libc.so.6": ["GLIBC_2.27", "GLIBC_2.9"]})

    def test_irrelevant_version_strings_do_not_raise_the_floor(self):
        data = fixture()
        self.assertEqual(self.parse(data), self.parse(data+b"GLIBC_999.99\0"))

    def test_extra_glibc_requirements_are_not_reported_as_numeric_compatibility(self):
        data = fixture()
        name = data.index(b"GLIBC_ABI_DT_RELR")-512
        struct.pack_into("<I", data, 808, name)
        result = self.parse(data)
        self.assertEqual(result["glibc_symbol_floor"], "2.9")
        self.assertEqual(result["glibc_extra_requirements"], ["GLIBC_ABI_DT_RELR"])

    def test_no_interpreter_or_dynamic_table(self):
        data = fixture()
        struct.pack_into("<H", data, 56, 1)
        result = self.parse(data)
        self.assertIsNone(result["interpreter"])
        self.assertEqual(result["needed_libraries"], [])
        self.assertIsNone(result["glibc_symbol_floor"])

    def test_truncated_and_extended_program_header_tables_refused(self):
        data = fixture()
        self.rejects(data[:100], "outside file")
        struct.pack_into("<H", data, 56, 65535)
        self.rejects(data, "program headers")

    def test_duplicate_interpreter_refused(self):
        data = fixture()
        data[176:232] = data[120:176]
        self.rejects(data, "interpreter segments")

    def test_interpreter_must_terminate_inside_its_segment(self):
        data = fixture()
        data[256+len(b"/lib64/ld-linux-x86-64.so.2")] = 88
        self.rejects(data, "Unterminated")

    def test_unmapped_string_table_refused(self):
        data = fixture()
        struct.pack_into("<Q", data, 344, 0xffffffffffffffff)
        self.rejects(data, "not uniquely file-backed")

    def test_dynamic_segment_must_describe_the_actual_loaded_table(self):
        data = fixture()
        struct.pack_into("<Q", data, 192, 0x400380)
        self.rejects(data, "offset disagrees with load address")
        data = fixture()
        struct.pack_into("<Q", data, 192, 0x500000)
        self.rejects(data, "not uniquely file-backed")
        data = fixture()
        struct.pack_into("<Q", data, 216, 16)
        self.rejects(data, "file size exceeds memory size")

    def test_string_table_offsets_must_be_in_range(self):
        data = fixture()
        struct.pack_into("<Q", data, 328, 128)
        self.rejects(data, "string offset out of range")

    def test_dynamic_table_requires_terminator(self):
        data = fixture()
        struct.pack_into("<qQ", data, 400, 1, 1)
        self.rejects(data, "no DT_NULL")

    def test_version_chain_cannot_escape_its_declared_count(self):
        data = fixture()
        struct.pack_into("<I", data, 796, 0)
        self.rejects(data, "auxiliary chain")
        data = fixture()
        struct.pack_into("<I", data, 812, 16)
        self.rejects(data, "auxiliary chain")

    def test_version_structures_and_library_must_be_valid(self):
        data = fixture()
        struct.pack_into("<H", data, 768, 2)
        self.rejects(data, "Invalid ELF version requirement")
        data = fixture()
        struct.pack_into("<I", data, 772, data.index(b"GLIBC_2.9")-512)
        self.rejects(data, "undeclared library")


if __name__ == "__main__":
    unittest.main()
