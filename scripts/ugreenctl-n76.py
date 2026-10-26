#!/usr/bin/env python3
"""Manually build a model-specific N76 module set; never install or load it."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import signal
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
from contextlib import contextmanager


MODELS = ("DH2600", "DXP480T Plus")
STANDARD_PATH = "/usr/bin:/bin:/usr/sbin:/sbin"
RELEASE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._+\-]{0,127}\Z")
KBUILD_PATH = re.compile(r"/[A-Za-z0-9_./+\-]+\Z")


class BuildError(RuntimeError):
    pass


def preparation_module():
    here = Path(__file__).resolve()
    candidates = (
        here.parent.parent / "tools/n76/prepare_sources.py",
        here.parent.parent / "share/ugreenctl/n76/prepare_sources.py",
    )
    for filename in candidates:
        if filename.is_file():
            spec = importlib.util.spec_from_file_location("ugreenctl_n76_sources", filename)
            module = importlib.util.module_from_spec(spec)
            # Reading a plan must not create __pycache__ in the tool directory.
            previous = sys.dont_write_bytecode
            try:
                sys.dont_write_bytecode = True
                spec.loader.exec_module(module)
            finally:
                sys.dont_write_bytecode = previous
            return module
    raise BuildError("N76 source preparation component is missing; retain the complete tool directory")


def bounded_text(filename: Path, maximum: int = 1024 * 1024) -> str:
    with filename.open("rb") as stream:
        data = stream.read(maximum + 1)
    if len(data) > maximum:
        raise BuildError(f"oversized metadata: {filename}")
    return data.decode("utf-8", errors="strict")


def read_model() -> str:
    product = bounded_text(Path("/sys/class/dmi/id/product_name"), 256).removesuffix("\n")
    if product not in MODELS:
        raise BuildError(f"N76 build requires exact DMI DH2600 or DXP480T Plus; detected {product!r}")
    return product


def read_kernel_release(headers: Path) -> str:
    release_file = headers / "include/config/kernel.release"
    uts_file = headers / "include/generated/utsrelease.h"
    release = bounded_text(release_file, 256).strip()
    uts = bounded_text(uts_file, 1024)
    match = re.fullmatch(r'\s*#define\s+UTS_RELEASE\s+"([^"\r\n]+)"\s*', uts)
    if not RELEASE.fullmatch(release) or not match or match[1] != release:
        raise BuildError("prepared kernel release metadata is invalid or inconsistent")
    return release


def safe_kbuild_path(path: Path) -> Path:
    # Kbuild interpolates paths into make syntax and shell recipes. Passing an
    # argv list alone does not make arbitrary path characters safe there.
    if not path.is_absolute() or not KBUILD_PATH.fullmatch(str(path)):
        raise BuildError("Kbuild paths require absolute paths containing only ASCII letters, digits, / . _ + -")
    return path


def inspect_host(kernel_build: Path | None = None) -> dict:
    if sys.platform != "linux":
        raise BuildError("N76 module compilation requires Linux")
    uname = os.uname()
    if uname.machine not in ("x86_64", "amd64"):
        raise BuildError("the two evidenced N76 models require Linux x86_64")
    if not RELEASE.fullmatch(uname.release):
        raise BuildError("unsupported running kernel release format")
    model = read_model()
    requested = kernel_build or Path("/lib/modules") / uname.release / "build"
    if not requested.is_absolute():
        raise BuildError("--kernel-build must be an absolute path")
    headers = safe_kbuild_path(requested.resolve())
    result = {
        "model": model, "architecture": "x86_64", "kernel_release": uname.release,
        "kernel_build": str(headers), "missing": [], "compiler": "gcc",
    }
    missing = result["missing"]
    required = ("Makefile", "include/config/kernel.release", "include/generated/utsrelease.h",
                "include/config/auto.conf", "include/generated/autoconf.h", "Module.symvers",
                "scripts/mod/modpost", "scripts/basic/fixdep")
    for name in required:
        filename = headers / name
        if not filename.is_file() or filename.stat().st_size == 0:
            missing.append(f"prepared kernel build file: {name}")
    if not missing:
        actual = read_kernel_release(headers)
        if actual != uname.release:
            raise BuildError(f"headers target {actual!r}, running kernel is {uname.release!r}")
        config = bounded_text(headers / "include/config/auto.conf")
        if not re.search(r"(?m)^CONFIG_MODULES=y$", config):
            raise BuildError("the target kernel configuration does not enable loadable modules")
        if re.search(r"(?m)^CONFIG_CC_IS_CLANG=y$", config):
            result["compiler"] = "clang"
        for name in ("scripts/mod/modpost", "scripts/basic/fixdep"):
            if not os.access(headers / name, os.X_OK):
                missing.append(f"kernel build helper is not executable: {name}")
        if re.search(r"(?m)^CONFIG_OBJTOOL=y$", config) and not os.access(headers / "tools/objtool/objtool", os.X_OK):
            missing.append("kernel build helper: tools/objtool/objtool")
    tools = ("make", "clang", "ld.lld", "llvm-ar") if result["compiler"] == "clang" else ("make", "gcc", "ld")
    result["tools"] = {}
    for name in tools:
        located = shutil.which(name, path=STANDARD_PATH)
        if located is None:
            missing.append(f"build tool: {name}")
        else:
            result["tools"][name] = located
    result["can_build"] = not missing
    return result


def clean_environment() -> dict[str, str]:
    # No inherited MAKEFILES/MAKEFLAGS/KBUILD_*/CC/ARCH/CROSS_COMPILE flags.
    return {"PATH": STANDARD_PATH, "HOME": str(Path.home()), "LANG": "C", "LC_ALL": "C",
            "TZ": "UTC", "KCONFIG_NOSILENTUPDATE": "1"}


def work_path(value: Path | None) -> Path:
    result = value or Path.home() / ".cache/ugreenctl/n76"
    if not result.is_absolute():
        raise BuildError("--work-dir must be an absolute path")
    return safe_kbuild_path(result)


def private_work_directory(path: Path, headers: Path) -> Path:
    for forbidden in (Path("/boot"), Path("/lib/modules"), Path("/sys"), Path("/proc"), Path("/dev")):
        if path.resolve().is_relative_to(forbidden.resolve()):
            raise BuildError(f"build workspace cannot be inside {forbidden}")
    resolved = path.resolve()
    if resolved.is_relative_to(headers) or headers.is_relative_to(resolved):
        raise BuildError("build workspace must be separate from the prepared kernel tree")
    # Existing ancestors must not be replaceable by other users. Sticky /tmp is
    # permitted; this command creates its own private directory below it.
    for ancestor in reversed((path, *path.parents)):
        if ancestor.exists():
            entry = ancestor.lstat()
            if stat.S_ISLNK(entry.st_mode) or not stat.S_ISDIR(entry.st_mode):
                raise BuildError(f"workspace ancestor must be a real directory: {ancestor}")
            if entry.st_uid not in (0, os.getuid()):
                raise BuildError(f"workspace ancestor belongs to another user: {ancestor}")
            if entry.st_mode & 0o022 and not entry.st_mode & stat.S_ISVTX:
                raise BuildError(f"workspace ancestor is writable by other users: {ancestor}")
        else:
            ancestor.mkdir(mode=0o700)
    info = path.lstat()
    if info.st_uid != os.getuid() or info.st_mode & 0o077:
        raise BuildError("workspace must be owned by the current user with mode 0700")
    return resolved


@contextmanager
def build_lock(base: Path):
    import fcntl
    fd = os.open(base / ".compile.lock", os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or info.st_uid != os.getuid() or info.st_mode & 0o077:
            raise BuildError("unsafe build lock")
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            raise BuildError("another N76 build is using this workspace") from exc
        yield
    finally:
        os.close(fd)


def module_metadata(filename: Path, release: str) -> dict:
    info = filename.lstat()
    if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or not 64 <= info.st_size <= 32 * 1024 * 1024:
        raise BuildError(f"invalid module output: {filename.name}")
    data = filename.read_bytes()
    if data[:7] != b"\x7fELF\x02\x01\x01" or struct.unpack_from("<HH", data, 16) != (1, 62):
        raise BuildError(f"module is not an x86_64 ELF relocatable: {filename.name}")
    table = struct.unpack_from("<Q", data, 40)[0]
    entry_size, count, names_index = struct.unpack_from("<HHH", data, 58)
    if entry_size != 64 or not 0 < names_index < count <= 8192 or table + count * 64 > len(data):
        raise BuildError("invalid ELF section table")
    sections = [struct.unpack_from("<IIQQQQIIQQ", data, table + i * 64) for i in range(count)]
    def contents(section):
        offset, size = section[4:6]
        if offset + size > len(data):
            raise BuildError("ELF section exceeds the module file")
        return data[offset:offset + size]
    names = contents(sections[names_index])
    matches = []
    for section in sections:
        offset = section[0]
        if offset >= len(names) or b"\0" not in names[offset:]:
            raise BuildError("invalid ELF section name")
        if names[offset:].split(b"\0", 1)[0] == b".modinfo":
            matches.append(contents(section))
    if len(matches) != 1:
        raise BuildError("module must contain exactly one .modinfo section")
    fields = {}
    for entry in matches[0].split(b"\0"):
        if entry:
            key, separator, value = entry.partition(b"=")
            if not separator:
                raise BuildError("invalid module metadata")
            fields.setdefault(key.decode("ascii"), []).append(value.decode("ascii"))
    expected = filename.stem.replace("-", "_")
    if fields.get("name") != [expected] or len(fields.get("vermagic", [])) != 1:
        raise BuildError("module name or vermagic is missing or inconsistent")
    if not fields["vermagic"][0].split() or fields["vermagic"][0].split()[0] != release:
        raise BuildError("compiled module targets a different kernel release")
    if len(fields.get("license", [])) != 1 or not fields["license"][0].startswith("GPL"):
        raise BuildError("compiled module license is missing or unexpected")
    return {"file": filename.name, "size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
            "name": expected, "vermagic": fields["vermagic"][0], "license": fields["license"][0]}


def run_build(command: list[str], run: Path, log: Path) -> int:
    with log.open("xb") as stream:
        process = subprocess.Popen(command, env=clean_environment(), cwd=run, stdin=subprocess.DEVNULL,
                                   stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            return process.wait(timeout=1800)
        except (subprocess.TimeoutExpired, KeyboardInterrupt):
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
            raise BuildError(f"build interrupted or exceeded 30 minutes; process group stopped; inspect {log}")


def compile_modules(host: dict, prep, base: Path, *, jobs: int, offline: bool) -> dict:
    base = private_work_directory(base, Path(host["kernel_build"]))
    with build_lock(base):
        run = Path(tempfile.mkdtemp(prefix=f"{host['kernel_release']}-", dir=base))
        source = run / "source"
        result = {"schema_version": 1, "status": "failed", "host": host, "build_directory": str(run),
                  "installed": False, "loaded": False, "hardware_validation": False}
        try:
            result["source_preparation"] = prep.prepare_sources(host["model"], base / "cache", source,
                                                               allow_download=not offline)
            # Source preparation and download must not silently rebind to a new
            # kernel/model while a lengthy build is being initiated.
            if inspect_host(Path(host["kernel_build"])) != host:
                raise BuildError("model, running kernel or build prerequisites changed")
            command = [host["tools"]["make"], "-C", host["kernel_build"], f"M={source}", f"-j{jobs}"]
            if host["compiler"] == "clang":
                command.append("LLVM=1")
            else:
                command.append(f"CC={host['tools']['gcc']}")
            command.append("modules")
            result["command"] = command
            log = run / "build.log"
            result["log"] = str(log)
            result["returncode"] = run_build(command, run, log)
            if result["returncode"]:
                raise BuildError(f"Kbuild failed with exit {result['returncode']}; inspect {log}")
            expected = prep.source_plan(host["model"])["modules"]
            if {p.name for p in source.glob("*.ko")} != set(expected):
                raise BuildError("Kbuild did not produce exactly the selected module set")
            result["modules"] = [module_metadata(source / name, host["kernel_release"]) for name in expected]
            result["kernel_unchanged_after_build"] = inspect_host(Path(host["kernel_build"])) == host
            if not result["kernel_unchanged_after_build"]:
                raise BuildError("kernel/model prerequisites changed during compilation")
            result["status"] = "built-not-installed"
        except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as exc:
            result["error"] = str(exc)
        (run / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        return result


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("check", "build"))
    parser.add_argument("--apply", action="store_true", help="Permit downloads and compilation; never install/load")
    parser.add_argument("--kernel-build", type=Path, help="Prepared headers for the running kernel (absolute path)")
    parser.add_argument("--work-dir", type=Path, help="Private build/cache directory, preferably on persistent storage")
    parser.add_argument("--offline", action="store_true", help="Use only an existing SHA-verified source cache")
    parser.add_argument("--jobs", type=int, choices=range(1, 17), default=2, metavar="1..16")
    args = parser.parse_args(argv)
    try:
        if args.command == "check" and args.apply:
            raise BuildError("check is read-only; --apply belongs to build")
        prep = preparation_module()
        host = inspect_host(args.kernel_build)
        base = work_path(args.work_dir)
        result = {"schema_version": 1, "status": "plan", "dry_run": True, "host": host,
                  "work_directory": str(base), "sources": prep.source_plan(host["model"]),
                  "installed": False, "loaded": False, "hardware_validation": False}
        if args.command == "build" and args.apply:
            if not host["can_build"]:
                raise BuildError("missing build prerequisites: " + "; ".join(host["missing"]))
            result = compile_modules(host, prep, base, jobs=args.jobs, offline=args.offline)
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0 if host["can_build"] and "error" not in result else 1
    except (OSError, ValueError, RuntimeError) as exc:
        print(json.dumps({"status": "error", "error": str(exc), "installed": False, "loaded": False}, ensure_ascii=False))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
