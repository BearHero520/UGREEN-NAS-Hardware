"""Build orchestration policy tests; no hardware, module loading or downloads."""

import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path, PurePosixPath
import struct
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("n76_build", ROOT / "scripts/ugreenctl-n76.py")
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


def module_bytes(name="test_module", release="6.18.15", license="GPL v2"):
    names = b"\0.shstrtab\0.modinfo\0"
    metadata = f"name={name}\0vermagic={release} SMP modversions\0license={license}\0".encode("ascii")
    data = bytearray(64 + len(names) + len(metadata))
    data[:7] = b"\x7fELF\x02\x01\x01"
    struct.pack_into("<HHI", data, 16, 1, 62, 1)
    data[64:64 + len(names)] = names
    data[64 + len(names):] = metadata
    table = len(data)
    struct.pack_into("<Q", data, 40, table)
    struct.pack_into("<HHH", data, 58, 64, 3, 1)
    data += bytes(64)
    data += struct.pack("<IIQQQQIIQQ", 1, 3, 0, 0, 64, len(names), 0, 0, 1, 0)
    data += struct.pack("<IIQQQQIIQQ", 11, 1, 0, 0, 64 + len(names), len(metadata), 0, 0, 1, 0)
    return bytes(data)


class BuildPolicyTests(unittest.TestCase):
    def test_resources_present_and_models_exact(self):
        prep = build.preparation_module()
        for model in build.MODELS:
            plan = prep.source_plan(model)
            self.assertEqual(len(plan["modules"]), 3)
            self.assertEqual(len(plan["sources"]), 3)
        with self.assertRaises(ValueError):
            prep.source_plan("DXP4800 Plus")

    def test_source_loader_does_not_create_bytecode(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "scripts").mkdir()
            resources = root / "tools/n76"
            resources.mkdir(parents=True)
            (resources / "prepare_sources.py").write_text("value = 7\n", encoding="utf-8")
            with patch.object(build, "__file__", str(root / "scripts/ugreenctl-n76.py")):
                self.assertEqual(build.preparation_module().value, 7)
            self.assertEqual(sorted(path.name for path in resources.iterdir()), ["prepare_sources.py"])

    def test_make_paths_refuse_injection_and_whitespace(self):
        for path in ("/mnt/build space", "/mnt/build$(touch)", "/mnt/a;b", "/mnt/a#b",
                     "/mnt/a\nb", "/mnt/磁盘", "/mnt/a:b", "/mnt/a\\b", "relative"):
            with self.subTest(path=path), self.assertRaises(build.BuildError):
                build.safe_kbuild_path(PurePosixPath(path))
        self.assertEqual(str(build.safe_kbuild_path(PurePosixPath("/mnt/pool/build-6.18+1"))),
                         "/mnt/pool/build-6.18+1")

    def test_environment_does_not_inherit_kbuild_overrides(self):
        with patch.dict(os.environ, {"MAKEFLAGS": "-i", "MAKEFILES": "/tmp/evil", "ARCH": "arm64",
                                    "KBUILD_MODPOST_WARN": "1", "CC": "arbitrary-command"}):
            env = build.clean_environment()
        for name in ("MAKEFLAGS", "MAKEFILES", "ARCH", "KBUILD_MODPOST_WARN", "CC"):
            self.assertNotIn(name, env)
        self.assertEqual(env["KCONFIG_NOSILENTUPDATE"], "1")

    def test_wrong_dmi_refused(self):
        for product in ("DXP4800 Plus\n", "DH2600 \n", " DXP480T Plus\n", "DH2600\r\n"):
            with self.subTest(product=product), patch.object(build, "bounded_text", return_value=product):
                with self.assertRaisesRegex(build.BuildError, "exact DMI"):
                    build.read_model()

    def test_known_dmi_accepted(self):
        for name in build.MODELS:
            with patch.object(build, "bounded_text", return_value=name + "\n"):
                self.assertEqual(build.read_model(), name)

    def test_non_linux_refused(self):
        with patch.object(build.sys, "platform", "win32"):
            with self.assertRaisesRegex(build.BuildError, "requires Linux"):
                build.inspect_host()

    def test_wrong_architecture_refused(self):
        with patch.object(build.sys, "platform", "linux"), patch.object(build.os, "uname", create=True,
                 return_value=types.SimpleNamespace(machine="aarch64", release="6.18.15")):
            with self.assertRaisesRegex(build.BuildError, "x86_64"):
                build.inspect_host()

    def host_fixture(self, directory, release="6.18.15", config="CONFIG_MODULES=y\n"):
        for filename, data in {
            "Makefile": "# fixture\n", "include/config/kernel.release": release,
            "include/generated/utsrelease.h": f'#define UTS_RELEASE "{release}"\n',
            "include/config/auto.conf": config, "include/generated/autoconf.h": "#define CONFIG_MODULES 1\n",
            "Module.symvers": "fixture\n", "scripts/mod/modpost": "fixture\n", "scripts/basic/fixdep": "fixture\n",
        }.items():
            path = directory / filename
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data.encode("utf-8"))
            if filename.startswith("scripts/"):
                path.chmod(0o755)

    @contextlib.contextmanager
    def mocked_linux(self):
        # Only path syntax is substituted on the Windows test host; separate
        # pure-posix tests verify the production Kbuild path restrictions.
        with patch.object(build.sys, "platform", "linux"), patch.object(build.os, "uname", create=True,
                 return_value=types.SimpleNamespace(machine="x86_64", release="6.18.15")), \
             patch.object(build, "read_model", return_value="DH2600"), \
             patch.object(build, "safe_kbuild_path", side_effect=lambda path: path), \
             patch.object(build.shutil, "which", side_effect=lambda name, **kw: "/usr/bin/" + name):
            yield

    def test_matching_prepared_headers(self):
        with tempfile.TemporaryDirectory() as tmp, self.mocked_linux():
            self.host_fixture(Path(tmp))
            result = build.inspect_host(Path(tmp))
            self.assertTrue(result["can_build"])
            self.assertEqual(result["compiler"], "gcc")

    def test_header_mismatch_refused(self):
        with tempfile.TemporaryDirectory() as tmp, self.mocked_linux():
            self.host_fixture(Path(tmp), release="6.18.14")
            with self.assertRaisesRegex(build.BuildError, "running kernel"):
                build.inspect_host(Path(tmp))

    def test_inconsistent_header_metadata_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            self.host_fixture(directory)
            (directory / "include/generated/utsrelease.h").write_text('#define UTS_RELEASE "wrong"')
            with self.assertRaisesRegex(build.BuildError, "inconsistent"):
                build.read_kernel_release(directory)

    def test_missing_symvers_reported(self):
        with tempfile.TemporaryDirectory() as tmp, self.mocked_linux():
            directory = Path(tmp)
            self.host_fixture(directory)
            (directory / "Module.symvers").unlink()
            result = build.inspect_host(directory)
            self.assertFalse(result["can_build"])
            self.assertIn("prepared kernel build file: Module.symvers", result["missing"])

    def test_modules_disabled_refused(self):
        with tempfile.TemporaryDirectory() as tmp, self.mocked_linux():
            self.host_fixture(Path(tmp), config="CONFIG_SMP=y\n")
            with self.assertRaisesRegex(build.BuildError, "does not enable"):
                build.inspect_host(Path(tmp))

    def test_clang_kernel_selects_llvm_tools(self):
        with tempfile.TemporaryDirectory() as tmp, self.mocked_linux():
            self.host_fixture(Path(tmp), config="CONFIG_MODULES=y\nCONFIG_CC_IS_CLANG=y\n")
            result = build.inspect_host(Path(tmp))
            self.assertEqual(result["compiler"], "clang")
            self.assertIn("ld.lld", result["tools"])

    def test_dry_run_no_download_compile_or_directory_creation(self):
        host = {"model": "DH2600", "can_build": True}
        for command in ("check", "build"):
            with self.subTest(command=command), patch.object(build, "inspect_host", return_value=host), \
                 patch.object(build, "work_path", return_value=Path("/uncreated")), \
                 patch.object(build, "compile_modules", side_effect=AssertionError("compile must not run")), \
                 contextlib.redirect_stdout(io.StringIO()) as output:
                self.assertEqual(build.main([command]), 0)
                self.assertTrue(json.loads(output.getvalue())["dry_run"])

    def test_apply_with_missing_prerequisites_refused(self):
        with patch.object(build, "inspect_host", return_value={"model": "DH2600", "can_build": False, "missing": ["headers"]}), \
             patch.object(build, "work_path", return_value=Path("/uncreated")), \
             patch.object(build, "compile_modules", side_effect=AssertionError("must not compile")), \
             contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(build.main(["build", "--apply"]), 1)
            self.assertIn("missing build prerequisites", output.getvalue())

    def test_check_apply_refused(self):
        with contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(build.main(["check", "--apply"]), 1)
            self.assertIn("read-only", output.getvalue())

    def test_module_metadata_matches_target(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "test-module.ko"
            path.write_bytes(module_bytes())
            result = build.module_metadata(path, "6.18.15")
            self.assertEqual(result["name"], "test_module")
            self.assertEqual(result["size"], path.stat().st_size)

    def test_module_output_mutations_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "test-module.ko"
            cases = [b"not an ELF", module_bytes(name="wrong"), module_bytes(release="6.18.14"),
                     module_bytes(license="Proprietary"), module_bytes()[:-12]]
            wrong_arch = bytearray(module_bytes())
            struct.pack_into("<H", wrong_arch, 18, 183)
            cases.append(bytes(wrong_arch))
            for data in cases:
                with self.subTest(size=len(data)):
                    path.write_bytes(data)
                    with self.assertRaises(build.BuildError):
                        build.module_metadata(path, "6.18.15")

    def test_compile_result_failure_and_identity_change(self):
        # Run the real orchestration with a fake compiler producing a synthetic
        # ELF. This verifies failure/status policy, not actual compilation.
        for failure in ("compiler", "before", "after", "success"):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as tmp:
                base = Path(tmp)
                host = {"model": "DH2600", "kernel_release": "6.18.15", "kernel_build": "/headers",
                        "compiler": "gcc", "tools": {"make": "/usr/bin/make", "gcc": "/usr/bin/gcc"}}
                def prepare(model, cache, source, **kwargs):
                    source.mkdir()
                    return {"test_fixture": True}
                prep = types.SimpleNamespace(prepare_sources=prepare,
                                             source_plan=lambda model: {"modules": ["test-module.ko"]})
                def compile_fixture(command, run, log):
                    self.assertEqual(command[0], "/usr/bin/make")
                    self.assertEqual(command[-1], "modules")
                    log.write_bytes(b"test compiler fixture\n")
                    (run / "source/test-module.ko").write_bytes(module_bytes())
                    return 2 if failure == "compiler" else 0
                changed = dict(host, model="DXP480T Plus")
                states = [changed] if failure == "before" else [host, changed if failure == "after" else host]
                with patch.object(build, "private_work_directory", return_value=base), \
                     patch.object(build, "build_lock", return_value=contextlib.nullcontext()), \
                     patch.object(build, "inspect_host", side_effect=states), \
                     patch.object(build, "run_build", side_effect=compile_fixture) as compiler:
                    result = build.compile_modules(host, prep, base, jobs=2, offline=True)
                self.assertFalse(result["installed"])
                self.assertFalse(result["loaded"])
                self.assertEqual(result["status"], "built-not-installed" if failure == "success" else "failed")
                self.assertEqual(compiler.call_count, 0 if failure == "before" else 1)
                self.assertTrue((Path(result["build_directory"]) / "result.json").is_file())

    @unittest.skipUnless(sys.platform == "linux", "real Linux filesystem/flock checks")
    def test_private_workspace_and_lock(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "private"
            build.private_work_directory(path, Path("/unrelated/kernel"))
            self.assertEqual(path.stat().st_mode & 0o777, 0o700)
            with build.build_lock(path):
                with self.assertRaisesRegex(build.BuildError, "another N76 build"):
                    with build.build_lock(path):
                        self.fail("second lock acquired")

    @unittest.skipUnless(sys.platform == "linux", "real Linux filesystem checks")
    def test_workspace_unsafe_permissions_symlink_and_header_overlap(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            unsafe = root / "unsafe"
            unsafe.mkdir(mode=0o755)
            unsafe.chmod(0o755)
            with self.assertRaisesRegex(build.BuildError, "0700"):
                build.private_work_directory(unsafe, Path("/unrelated/kernel"))
            link = root / "link"
            link.symlink_to(root, target_is_directory=True)
            with self.assertRaisesRegex(build.BuildError, "real directory"):
                build.private_work_directory(link, Path("/unrelated/kernel"))
            with self.assertRaisesRegex(build.BuildError, "separate"):
                build.private_work_directory(root / "source", root)


if __name__ == "__main__":
    unittest.main(verbosity=2)
