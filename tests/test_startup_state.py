"""Offline persistence tests: injected OS contracts on Windows; real files on Linux/root.

No test invokes ugreenctl or reads the host DMI.  The Linux-only cases exercise
dirfd/O_NOFOLLOW/flock/fsync using a disposable private directory below /root.
The mock cases do not establish real Linux filesystem or NAS validation.
"""

import contextlib
import copy
import errno
import importlib.util
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import types
import unittest
from unittest import mock


SPEC = importlib.util.spec_from_file_location(
    "ugreenctl_startup", Path(__file__).resolve().parents[1] / "scripts/ugreenctl-startup.py")
startup = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(startup)

STATE_DIR = "/var/lib/ugreenctl/startup"
STATE_PATH = STATE_DIR + "/" + startup.STATE_FILE
LOCK_PATH = STATE_DIR + "/" + startup.LOCK_FILE
SUPPORTED_PRODUCTS = ("DX4600", "DX4600+", "DX4600 Pro", "DXP4800",
                      "DXP4800S", "DXP4800 Plus", "DXP4800 Pro", "DXP480T Plus",
                      "DXP6800 Plus", "DXP6800 Pro")


def identity(product="DX4600"):
    return {"product_name": product, "product_version": "fixture-revision",
            "board_version": "fixture-board", "bios_vendor": "fixture-vendor",
            "bios_version": "fixture-bios", "bios_date": "01/01/2026"}


def record(policy="off", product="DX4600"):
    return {"schema_version": 1, "desired_policy": policy, "identity": identity(product)}


class FakePosix:
    """In-memory OS call contract; it deliberately cannot access a host file."""
    O_RDONLY, O_WRONLY, O_RDWR = 0, 1, 2
    O_CREAT, O_EXCL = 0x40, 0x80
    O_NONBLOCK, O_DIRECTORY, O_NOFOLLOW, O_CLOEXEC = 0x800, 0x10000, 0x20000, 0x80000

    def __init__(self, configured=True):
        self.nodes, self.fds, self.events = {}, {}, []
        self.next_fd = 10
        self.fail_write = False
        self.fail_replace = False
        self.fail_file_sync = False
        self.fail_directory_sync_after_replace = False
        self.replaced = False
        for path in ("/", "/var", "/var/lib", "/var/lib/ugreenctl"):
            self.add(path, "dir", 0o755)
        if configured:
            self.add(STATE_DIR, "dir", 0o700)
            self.state(record())

    def add(self, path, kind="file", mode=0o600, uid=0, nlink=1, data=b""):
        self.nodes[path] = {"path": path, "kind": kind, "mode": mode, "uid": uid,
                            "nlink": nlink, "data": data}

    def state(self, value):
        data = json.dumps(value).encode("utf-8") if not isinstance(value, bytes) else value
        self.add(STATE_PATH, data=data)

    def path(self, name, dir_fd=None):
        if name.startswith("/"):
            return name
        return self.fds[dir_fd]["node"]["path"].rstrip("/") + "/" + name

    def open(self, name, flags, mode=0o777, dir_fd=None):
        path = self.path(name, dir_fd)
        self.events.append(("open", path, flags))
        node = self.nodes.get(path)
        if node is None:
            if not flags & self.O_CREAT:
                raise FileNotFoundError(path)
            self.add(path, mode=mode)
            self.events.append(("create", path))
            node = self.nodes[path]
        elif flags & self.O_CREAT and flags & self.O_EXCL:
            raise FileExistsError(path)
        if node["kind"] == "symlink" and flags & self.O_NOFOLLOW:
            raise OSError(errno.ELOOP, "symlink refused")
        if flags & self.O_DIRECTORY and node["kind"] != "dir":
            raise OSError(errno.ENOTDIR, "not directory")
        self.next_fd += 1
        self.fds[self.next_fd] = {"node": node, "offset": 0}
        return self.next_fd

    def fstat(self, fd):
        n = self.fds[fd]["node"]
        kind = {"dir": stat.S_IFDIR, "file": stat.S_IFREG, "fifo": stat.S_IFIFO,
                "symlink": stat.S_IFLNK, "socket": stat.S_IFSOCK}[n["kind"]]
        return types.SimpleNamespace(st_mode=kind | n["mode"], st_uid=n["uid"],
                                     st_nlink=n["nlink"], st_size=len(n["data"]))

    def mkdir(self, name, mode, dir_fd=None):
        path = self.path(name, dir_fd)
        if path in self.nodes:
            raise FileExistsError(path)
        self.events.append(("mkdir", path))
        self.add(path, "dir", mode)

    def read(self, fd, size):
        item = self.fds[fd]
        self.events.append(("read", item["node"]["path"]))
        start = item["offset"]
        item["offset"] += size
        return item["node"]["data"][start:start + size]

    def write(self, fd, data):
        if self.fail_write:
            raise OSError("injected write failure")
        item = self.fds[fd]
        self.events.append(("write", item["node"]["path"]))
        # Exercise the caller's partial-write loop.
        count = min(17, len(data))
        item["node"]["data"] += data[:count]
        item["offset"] += count
        return count

    def fsync(self, fd):
        node = self.fds[fd]["node"]
        self.events.append(("fsync", node["path"]))
        if self.fail_file_sync and node["kind"] == "file":
            raise OSError("injected temporary fsync failure")
        if self.fail_directory_sync_after_replace and self.replaced and node["kind"] == "dir":
            raise OSError("injected directory fsync failure")

    def replace(self, source, destination, src_dir_fd=None, dst_dir_fd=None):
        if self.fail_replace:
            raise OSError("injected replace failure")
        source_path = self.path(source, src_dir_fd)
        target_path = self.path(destination, dst_dir_fd)
        self.events.append(("replace", target_path))
        if target_path in self.nodes:
            self.nodes[target_path]["nlink"] = 0
        node = self.nodes.pop(source_path)
        node["path"] = target_path
        self.nodes[target_path] = node
        self.replaced = True

    def unlink(self, name, dir_fd=None):
        path = self.path(name, dir_fd)
        if path not in self.nodes:
            raise FileNotFoundError(path)
        self.events.append(("unlink", path))
        del self.nodes[path]

    def close(self, fd):
        del self.fds[fd]

    def mutations(self):
        return [event for event in self.events
                if event[0] in ("mkdir", "create", "write", "replace", "unlink", "fsync")]


class FakeRuntime:
    def __init__(self, product="DX4600"):
        self.value = identity(product)
        self.snapshots = []
        self.root = True

    def require_root(self):
        if not self.root:
            raise startup.StartupError("root required")

    def identity(self):
        return copy.deepcopy(self.snapshots.pop(0) if self.snapshots else self.value)


class FakeBackend:
    def __init__(self):
        self.policy, self.calls, self.reads = "off", [], []
        self.set_error = None

    def get(self, allow_unknown=False):
        self.calls.append(("get", allow_unknown))
        value = self.reads.pop(0) if self.reads else self.policy
        if isinstance(value, Exception):
            raise value
        if value not in startup.POLICIES and not (allow_unknown and value == "unknown"):
            raise startup.StartupError("bad or unknown readback")
        return value

    def set(self, policy):
        self.calls.append(("set", policy))
        if self.set_error:
            raise self.set_error
        self.policy = policy

    def writes(self):
        return [call for call in self.calls if call[0] == "set"]


class StartupStateTests(unittest.TestCase):
    def setUp(self):
        self.api = FakePosix()
        self.runtime, self.backend = FakeRuntime(), FakeBackend()
        self.lock_events = []
        self.lock_busy = False

    def flock(self, fd, op):
        self.lock_events.append(op)
        if self.lock_busy and op != 8:
            raise BlockingIOError("busy")

    def store(self):
        return startup.LinuxStateStore(STATE_DIR, self.api, (self.flock, 5, 8))

    def run_action(self, command="set", policy="on", write=True):
        return startup.execute(command, policy, write, write,
                               self.store(), self.runtime, self.backend)

    def assert_zero_write_reject(self, regex=".", command="apply", policy=None):
        with self.assertRaisesRegex(startup.StartupError, regex):
            self.run_action(command, policy)
        self.assertEqual(self.backend.writes(), [])
        self.assertEqual(self.api.fds, {})

    def test_current_ten_product_three_policy_coordination(self):
        # Fixture names are coverage, not a new production model allowlist. Only
        # the existing binary determines backend support/chip/owner/readback.
        for product in SUPPORTED_PRODUCTS:
            for policy in startup.POLICIES:
                with self.subTest(product=product, policy=policy):
                    self.setUp()
                    self.runtime.value = identity(product)
                    result = self.run_action(policy=policy)
                    self.assertTrue(result["state_saved"])
                    saved = startup.decode_state(self.api.nodes[STATE_PATH]["data"])
                    self.assertEqual(saved, record(policy, product))
                    self.assertEqual(self.backend.writes(), [("set", policy)])
                    self.assertNotIn("current_hardware_policy", saved)

    def test_last_is_saved_as_restore(self):
        result = self.run_action(policy="last")
        self.assertEqual(result["desired_policy"], "restore")
        self.assertEqual(startup.decode_state(self.api.nodes[STATE_PATH]["data"])["desired_policy"], "restore")

    def test_no_saved_record_or_directory_no_backend_or_file_mutation(self):
        for existing_dir in (True, False):
            with self.subTest(existing_dir=existing_dir):
                self.setUp()
                del self.api.nodes[STATE_PATH]
                if not existing_dir:
                    del self.api.nodes[STATE_DIR]
                result = self.run_action("apply", None)
                self.assertFalse(result["configured"])
                self.assertEqual(self.backend.calls, [])
                self.assertEqual(self.api.mutations(), [])

    def test_reboot_start_stop_and_repeated_apply_reassert_saved_desire(self):
        self.run_action(policy="restore")
        saved = self.api.nodes[STATE_PATH]["data"]
        for overwritten in ("off", "unknown", "on", "restore", "unknown"):
            with self.subTest(firmware_policy=overwritten):
                self.backend.policy = overwritten
                result = self.run_action("apply", None)
                self.assertEqual(result["current_hardware_policy"], "restore")
                self.assertTrue(result["hardware_written"])
                self.assertFalse(result["state_saved"])
                self.assertEqual(self.api.nodes[STATE_PATH]["data"], saved)

    def test_initial_unknown_does_not_prevent_explicit_set(self):
        self.backend.policy = "unknown"
        result = self.run_action(policy="on")
        self.assertEqual(result["current_hardware_policy"], "on")
        self.assertTrue(result["state_saved"])

    def test_dry_run_never_invokes_backend_or_creates_files_or_lock(self):
        for command, policy in (("set", "on"), ("apply", None), ("show", None)):
            with self.subTest(command=command):
                self.setUp()
                result = self.run_action(command, policy, write=False)
                self.assertTrue(result["dry_run"])
                self.assertEqual(result["current_hardware_policy"], "not_read")
                self.assertEqual(self.backend.calls, [])
                self.assertEqual(self.api.mutations(), [])
        self.api = FakePosix(configured=False)
        self.run_action(write=False)
        self.assertEqual(self.api.mutations(), [])

    def test_both_flags_required_and_show_never_executes(self):
        for force, apply in ((True, False), (False, True)):
            with self.assertRaisesRegex(startup.StartupError, "together"):
                startup.execute("set", "on", force, apply, self.store(), self.runtime, self.backend)
        self.assertEqual(self.api.events, [])
        self.run_action("show", None)
        self.assertEqual(self.backend.calls, [])
        self.assertEqual(self.api.mutations(), [])

    def test_missing_binary_explicit_write_rejects_before_creating_any_files(self):
        self.api = FakePosix(configured=False)
        with self.assertRaisesRegex(startup.StartupError, "--binary"):
            startup.execute("set", "on", True, True, self.store(), self.runtime)
        self.assertEqual(self.api.events, [])

    def test_unsupported_backend_or_preread_error_never_sets(self):
        for message in ("unsupported exact model", "chip mismatch", "owner conflict", "I/O failure", "timeout"):
            with self.subTest(message=message):
                self.setUp()
                self.backend.reads = [startup.StartupError(message)]
                old = self.api.nodes[STATE_PATH]["data"]
                self.assert_zero_write_reject(message, "set", "on")
                self.assertEqual(self.api.nodes[STATE_PATH]["data"], old)

    def test_postread_failure_unknown_mismatch_preserve_old_record(self):
        for readback in ("off", "unknown", startup.StartupError("readback timeout")):
            with self.subTest(readback=str(readback)):
                self.setUp()
                old = self.api.nodes[STATE_PATH]["data"]
                self.backend.reads = ["unknown", readback]
                with self.assertRaises(startup.StartupError) as caught:
                    self.run_action(policy="on")
                self.assertTrue(caught.exception.hardware_written)
                self.assertEqual(self.api.nodes[STATE_PATH]["data"], old)
                self.assertFalse(self.api.replaced)

    def test_failed_set_does_not_replace_old_record_or_run_postread(self):
        old = self.api.nodes[STATE_PATH]["data"]
        self.backend.set_error = startup.StartupError("set rejected")
        with self.assertRaisesRegex(startup.StartupError, "set rejected"):
            self.run_action()
        self.assertEqual(self.backend.calls, [("get", True), ("set", "on")])
        self.assertEqual(self.api.nodes[STATE_PATH]["data"], old)

    def test_every_identity_field_change_blocks_apply_but_show_explains(self):
        for name in startup.IDENTITY_FIELDS:
            with self.subTest(field=name):
                self.setUp()
                self.runtime.value[name] += "-changed"
                self.assert_zero_write_reject("identity changed")
                diagnostic = self.run_action("show", None)
                self.assertFalse(diagnostic["identity_matches"])
                self.assertEqual(diagnostic["current_hardware_policy"], "not_read")

    def test_explicit_set_rebinds_identity_only_after_successful_readback(self):
        self.runtime.value["bios_version"] = "administrator-new-BIOS"
        result = self.run_action()
        self.assertTrue(result["state_saved"])
        self.assertEqual(startup.decode_state(self.api.nodes[STATE_PATH]["data"])["identity"], self.runtime.value)

    def test_identity_change_during_preread_blocks_write(self):
        changed = identity()
        changed["bios_version"] = "changed-during-read"
        self.runtime.snapshots = [identity(), changed]
        self.assert_zero_write_reject("before the write")

    def test_identity_change_after_write_does_not_save(self):
        changed = identity()
        changed["board_version"] = "changed-after-write"
        self.runtime.snapshots = [identity(), identity(), changed]
        old = self.api.nodes[STATE_PATH]["data"]
        with self.assertRaisesRegex(startup.StartupError, "after the write") as caught:
            self.run_action()
        self.assertTrue(caught.exception.hardware_written)
        self.assertEqual(self.api.nodes[STATE_PATH]["data"], old)

    def test_strict_json_rejects_oversize_duplicates_schemas_and_values(self):
        good = json.dumps(record())
        invalid = [b"", b"{}", b"[1]", b"null", b"x" * 4097,
                   b"\xff", good.replace('"schema_version": 1', '"schema_version": true').encode(),
                   good.replace('"schema_version": 1', '"schema_version": 2').encode(),
                   good.replace('"desired_policy": "off"', '"desired_policy": "last"').encode(),
                   good.replace('"desired_policy": "off"', '"desired_policy": []').encode(),
                   good.replace('"schema_version": 1', '"schema_version": 1, "schema_version": 1').encode(),
                   good.replace('"bios_version": "fixture-bios"', '"bios_version": "x", "bios_version": "y"').encode(),
                   good.replace('"schema_version": 1', '"schema_version": NaN').encode(),
                   good.replace('"bios_date": "01/01/2026"', '"bios_date": "x\\ny"').encode(),
                   good.replace('"bios_date": "01/01/2026"', '"bios_date": "\\ud800"').encode(),
                   good.replace('"schema_version": 1', '"extra": 0, "schema_version": 1').encode()]
        bad_id = record(); del bad_id["identity"]["board_version"]
        invalid.append(json.dumps(bad_id).encode())
        for data in invalid:
            with self.subTest(data=data[:80]):
                self.setUp(); self.api.state(data)
                self.assert_zero_write_reject(command="set", policy="on")
                self.assertEqual(self.api.mutations(), [])

    def test_fifo_symlink_socket_permissions_owner_and_hardlink_reject_before_read(self):
        for change in ({"kind": "fifo"}, {"kind": "symlink"}, {"kind": "socket"},
                       {"kind": "dir"}, {"mode": 0o644}, {"mode": 0o666},
                       {"uid": 1000}, {"nlink": 2}):
            with self.subTest(change=change):
                self.setUp(); self.api.nodes[STATE_PATH].update(change)
                self.assert_zero_write_reject()
                self.assertFalse(any(e[0] == "read" for e in self.api.events))

    def test_unsafe_path_ancestors_final_directory_and_lock(self):
        for path, change in (("/var", {"mode": 0o777}), ("/var/lib", {"uid": 1000}),
                             ("/var/lib/ugreenctl", {"kind": "symlink"}),
                             (STATE_DIR, {"mode": 0o755})):
            with self.subTest(path=path, change=change):
                self.setUp(); self.api.nodes[path].update(change)
                self.assert_zero_write_reject()
        for change in ({"kind": "symlink"}, {"kind": "fifo"}, {"uid": 1},
                       {"nlink": 2}, {"mode": 0o400}, {"mode": 0o666}):
            with self.subTest(lock=change):
                self.setUp(); self.api.add(LOCK_PATH); self.api.nodes[LOCK_PATH].update(change)
                self.assert_zero_write_reject()

    def test_path_components_are_not_normalized_past_symlinks(self):
        for path in ("relative", "/", "/var//lib", "/var/../safe", "/var/./lib", "/var/lib/", "/x\x00"):
            with self.subTest(path=path):
                with self.assertRaises(startup.StartupError):
                    with startup.LinuxStateStore(path, self.api).open():
                        self.fail("unsafe path accepted")

    def test_nonroot_before_any_path_or_backend_access(self):
        self.runtime.root = False
        self.assert_zero_write_reject("root")
        self.assertEqual(self.api.events, [])
        self.assertEqual(self.backend.calls, [])

    def test_lock_is_exclusive_and_released_and_uses_nofollow_dirfd(self):
        self.run_action()
        self.assertEqual(self.lock_events, [5, 8])
        self.assertEqual(self.api.fds, {})
        for event in self.api.events:
            if event[0] == "open":
                self.assertTrue(event[2] & self.api.O_NOFOLLOW)
        self.setUp(); self.lock_busy = True
        self.assert_zero_write_reject("holds the lock")
        self.assertEqual(self.backend.calls, [])

    def test_atomic_save_sync_order_and_safe_mode(self):
        self.run_action()
        events = self.api.events
        replacement = next(i for i, e in enumerate(events) if e[0] == "replace")
        temp_sync = next(i for i, e in enumerate(events)
                         if e[0] == "fsync" and e[1].endswith(".tmp"))
        directory_sync = next(i for i, e in enumerate(events)
                              if e[0] == "fsync" and e[1] == STATE_DIR)
        self.assertLess(temp_sync, replacement)
        self.assertLess(replacement, directory_sync)
        self.assertEqual(self.api.nodes[STATE_PATH]["mode"], 0o600)
        self.assertEqual(self.api.nodes[STATE_PATH]["nlink"], 1)

    def test_save_failures_keep_old_record_before_replace_and_report_written_hardware(self):
        for setting in ("fail_write", "fail_file_sync", "fail_replace"):
            with self.subTest(failure=setting):
                self.setUp(); setattr(self.api, setting, True)
                old = self.api.nodes[STATE_PATH]["data"]
                with self.assertRaises(startup.StartupError) as caught:
                    self.run_action()
                self.assertTrue(caught.exception.hardware_written)
                self.assertFalse(caught.exception.persistence_uncertain)
                self.assertEqual(self.api.nodes[STATE_PATH]["data"], old)
                self.assertFalse(any(p.endswith(".tmp") for p in self.api.nodes))

    def test_post_replace_directory_sync_failure_reports_uncertainty(self):
        self.api.fail_directory_sync_after_replace = True
        with self.assertRaises(startup.StartupError) as caught:
            self.run_action()
        self.assertTrue(caught.exception.hardware_written)
        self.assertTrue(caught.exception.persistence_uncertain)

    def test_creation_is_private_and_only_explicit_write_creates_directory(self):
        self.api = FakePosix(configured=False)
        self.run_action()
        self.assertEqual(self.api.nodes[STATE_DIR]["mode"], 0o700)
        self.assertEqual(self.api.nodes[LOCK_PATH]["mode"], 0o600)
        self.assertEqual(self.api.nodes[STATE_PATH]["mode"], 0o600)


class BackendContractTests(unittest.TestCase):
    def run_output(self, output, code=0):
        return types.SimpleNamespace(returncode=code, stdout=output, stderr=b"fixture")

    def test_exact_binary_argv_flags_no_shell_no_model_or_inherited_environment(self):
        calls = []
        def runner(argv, **kwargs):
            calls.append((argv, kwargs))
            return self.run_output(b"unknown\n" if argv[-1] == "get" else b"set\n")
        backend = startup.Backend("/usr/local/bin/ugreenctl", "/usr/local/lib/ugreenctl/models", runner)
        self.assertEqual(backend.get(allow_unknown=True), "unknown")
        backend.set("restore")
        prefix = ["/usr/local/bin/ugreenctl", "--plugin-dir", "/usr/local/lib/ugreenctl/models"]
        self.assertEqual(calls[0][0], prefix + ["power", "startup", "get"])
        self.assertEqual(calls[1][0], prefix + ["--force", "--apply", "power", "startup", "set", "restore"])
        for argv, options in calls:
            self.assertFalse(options["shell"])
            self.assertNotIn("--model", argv)
            self.assertEqual(set(options["env"]), {"PATH", "LC_ALL", "LANG"})
            self.assertEqual(options["cwd"], "/")

    def test_get_requires_exact_token_not_json_extra_lines_last_or_garbled_data(self):
        for data in (b"unknown\n", b"last\n", b"ON\n", b"on\nwarning\n", b" on\n", b"on\r\n", b"", b"{}", b"\xff"):
            with self.subTest(data=data):
                backend = startup.Backend("/binary", runner=lambda *a, **k: self.run_output(data))
                with self.assertRaises(startup.StartupError):
                    backend.get()

    def test_exit_timeout_and_os_errors_are_not_accepted_as_readback(self):
        backend = startup.Backend("/binary", runner=lambda *a, **k: self.run_output(b"on\n", 1))
        with self.assertRaises(startup.StartupError):
            backend.get()
        for error in (OSError("missing"), subprocess.TimeoutExpired("fixture", 20)):
            with self.subTest(error=str(error)):
                def runner(*args, **kwargs):
                    raise error
                with self.assertRaises(startup.StartupError):
                    startup.Backend("/binary", runner=runner).get()

    def test_set_timeout_or_failure_is_an_attempt_not_confirmed_hardware_write(self):
        for failure in (False, True):
            def runner(*args, **kwargs):
                if failure:
                    raise subprocess.TimeoutExpired("fixture", 20)
                return self.run_output(b"", 1)
            with self.assertRaises(startup.StartupError) as caught:
                startup.Backend("/binary", runner=runner).set("on")
            self.assertTrue(caught.exception.hardware_write_attempted)
            self.assertIsNone(caught.exception.hardware_written)

    def test_backend_set_error_after_possible_write_keeps_old_desire_and_reports_unknown(self):
        for timeout in (False, True):
            with self.subTest(timeout=timeout):
                api = FakePosix()
                before = api.nodes[STATE_PATH]["data"]
                calls = []
                def runner(argv, **kwargs):
                    calls.append(argv)
                    if argv[-1] == "get":
                        return self.run_output(b"unknown\n")
                    if timeout:
                        raise subprocess.TimeoutExpired("fixture", 20)
                    # A real backend may return failure only after attempting the
                    # register transaction and detecting a bad low-level readback.
                    return self.run_output(b"", 1)
                backend = startup.Backend("/binary", runner=runner)
                store = startup.LinuxStateStore(STATE_DIR, api, (lambda *args: None, 5, 8))
                with self.assertRaises(startup.StartupError) as caught:
                    startup.execute("set", "on", True, True, store, FakeRuntime(), backend)
                self.assertTrue(caught.exception.hardware_write_attempted)
                self.assertIsNone(caught.exception.hardware_written)
                self.assertEqual(len(calls), 2)
                self.assertEqual(api.nodes[STATE_PATH]["data"], before)
                self.assertFalse(api.replaced)

    def test_cli_accepts_options_after_command_and_rejects_overrides(self):
        runtime = FakeRuntime()
        api = FakePosix()
        store = startup.LinuxStateStore(STATE_DIR, api, (lambda *args: None, 5, 8))
        backend = FakeBackend()
        with mock.patch.object(startup, "LinuxRuntime", return_value=runtime), \
                mock.patch.object(startup, "LinuxStateStore", return_value=store), \
                mock.patch.object(startup, "Backend", return_value=backend), \
                mock.patch("builtins.print") as printer:
            self.assertEqual(startup.main(["set", "last", "--binary", "/binary",
                                           "--force", "--apply"]), 0)
            output = json.loads(printer.call_args.args[0])
            self.assertEqual(output["desired_policy"], "restore")
            self.assertTrue(output["state_saved"])
        for option in ("--model", "--sysfs-root"):
            with mock.patch.object(sys, "stderr"), self.assertRaises(SystemExit) as caught:
                startup.main(["show", option, "unsafe"])
            self.assertEqual(caught.exception.code, 2)

    def test_relative_binary_or_plugins_rejected(self):
        for binary, plugins in (("ugreenctl", None), ("/usr/local/bin/ugreenctl", "models")):
            with self.assertRaises(startup.StartupError):
                startup.Backend(binary, plugins)

    def test_production_runtime_rejects_windows_or_nonroot(self):
        with mock.patch.object(startup.sys, "platform", "win32"):
            with self.assertRaisesRegex(startup.StartupError, "Linux"):
                startup.LinuxRuntime().require_root()
        with mock.patch.object(startup.sys, "platform", "linux"), \
                mock.patch.object(startup.os, "geteuid", return_value=1000, create=True):
            with self.assertRaisesRegex(startup.StartupError, "root"):
                startup.LinuxRuntime().require_root()

    def test_identity_reads_fixed_nonserial_paths_only(self):
        paths = []
        @contextlib.contextmanager
        def source(path, mode):
            import io
            paths.append(path)
            name = path.rsplit("/", 1)[-1]
            yield io.BytesIO((identity()[name] + "\n").encode())
        with mock.patch("builtins.open", side_effect=source):
            self.assertEqual(startup.LinuxRuntime().identity(), identity())
        self.assertEqual(paths, ["/sys/class/dmi/id/" + x for x in startup.IDENTITY_FIELDS])


@unittest.skipUnless(sys.platform == "linux" and hasattr(os, "geteuid") and os.geteuid() == 0,
                     "real Linux dirfd/flock tests require root; mock tests cover Windows contracts")
class RealLinuxFilesystemTests(unittest.TestCase):
    def setUp(self):
        # /tmp is intentionally forbidden by the production ancestor rules.
        self.root = tempfile.mkdtemp(prefix="ugreenctl-startup-offline-", dir="/root")
        os.chmod(self.root, 0o700)
        self.path = self.root + "/state"
        self.runtime, self.backend = FakeRuntime(), FakeBackend()
        self.store = startup.LinuxStateStore(self.path)

    def tearDown(self):
        shutil.rmtree(self.root)

    def execute(self, command="set", policy="on"):
        return startup.execute(command, policy, True, True, self.store, self.runtime, self.backend)

    def test_real_atomic_save_restart_apply_and_permissions(self):
        self.execute()
        path = Path(self.path) / startup.STATE_FILE
        self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(Path(self.path).stat().st_mode), 0o700)
        self.backend.policy = "unknown"
        self.execute("apply", None)
        self.assertEqual(self.backend.policy, "on")
        self.assertEqual(startup.decode_state(path.read_bytes())["desired_policy"], "on")

    def test_real_symlink_fifo_hardlink_and_bad_ancestor_refuse(self):
        self.execute()
        path = Path(self.path) / startup.STATE_FILE
        for kind in ("symlink", "fifo", "hardlink"):
            path.unlink()
            outside = Path(self.root) / "outside"
            outside.write_text("preserve")
            outside.chmod(0o600)
            if kind == "symlink":
                path.symlink_to(outside)
            elif kind == "fifo":
                os.mkfifo(path, 0o600)
            else:
                os.link(outside, path)
            self.backend.calls.clear()
            with self.assertRaises(startup.StartupError):
                self.execute("apply", None)
            self.assertEqual(self.backend.calls, [])
            self.assertEqual(outside.read_text(), "preserve")
            outside.unlink()
        path.unlink()
        os.chmod(self.root, 0o777)
        with self.assertRaises(startup.StartupError):
            self.execute()

    def test_real_flock_rejects_competing_process_lock(self):
        self.execute()
        import fcntl
        with open(Path(self.path) / startup.LOCK_FILE, "rb") as held:
            fcntl.flock(held.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            self.backend.calls.clear()
            with self.assertRaisesRegex(startup.StartupError, "holds the lock"):
                self.execute("apply", None)
            self.assertEqual(self.backend.calls, [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
