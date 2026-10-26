"""Run a cross-compiled Linux CTest in an isolated, disposable QEMU initramfs."""
import argparse
import gzip
import os
from pathlib import Path
import re
import shlex
import stat
import subprocess
import sys
import tarfile
import tempfile

def entry(name, mode, data=b'', ino=1):
    name = name.encode() + b'\0'
    fields = [ino, mode, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(name), 0]
    header = b'070701' + b''.join(f'{v:08x}'.encode() for v in fields)
    result = header + name
    result += b'\0' * (-len(result) % 4)
    result += data
    result += b'\0' * (-len(data) % 4)
    return result

def archive_files(path):
    result = {}
    with tarfile.open(path) as archive:
        for m in archive:
            name = m.name.removeprefix('./').rstrip('/')
            if not name or name.startswith('/') or '..' in name.split('/'):
                continue
            if m.isfile():
                result[name] = (stat.S_IFREG | m.mode, archive.extractfile(m).read())
            elif m.issym():
                result[name] = (stat.S_IFLNK | m.mode, m.linkname.encode())
            elif m.isdir():
                result[name] = (stat.S_IFDIR | m.mode, b'')
            elif m.islnk():
                original = result.get(m.linkname.removeprefix('./'))
                if original:
                    result[name] = original
    return result

def run(args, runtime_root, timeout=45):
    runtime_root = Path(runtime_root).resolve()
    exe = Path(args[0]).resolve()
    binary = exe.read_bytes()
    if (len(binary) < 64 or binary[:7] != b'\x7fELF\x02\x01\x01'
            or int.from_bytes(binary[18:20], 'little') not in (62, 183)):
        raise ValueError('Expected a Linux ELF64 little-endian x86_64 or aarch64 executable')
    arm64 = int.from_bytes(binary[18:20], 'little') == 183
    runtime = runtime_root / 'arm64' if arm64 else runtime_root
    triplet = 'aarch64-linux-gnu' if arm64 else 'x86_64-linux-gnu'
    files = archive_files(runtime / 'rootfs.tar.gz')
    files.update(archive_files(runtime / 'libc-data.tar' if arm64 else runtime_root / 'libc-deb/data.tar'))
    # Debian libc uses usr-merge; Alpine keeps a real /lib directory.
    for name, value in list(files.items()):
        if name.startswith('usr/lib/' + triplet + '/'):
            files[name.removeprefix('usr/')] = value
    files['lib/' + triplet] = (stat.S_IFDIR | 0o755, b'')
    if arm64:
        files['lib/ld-linux-aarch64.so.1'] = (
            stat.S_IFLNK | 0o777, b'/lib/aarch64-linux-gnu/ld-linux-aarch64.so.1')
    else:
        files['lib64'] = (stat.S_IFDIR | 0o755, b'')
        files['lib64/ld-linux-x86-64.so.2'] = (
            stat.S_IFLNK | 0o777, b'/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2')
    files['work'] = (stat.S_IFDIR | 0o755, b'')
    files['work/test'] = (stat.S_IFREG | 0o755, exe.read_bytes())
    if (exe.parent / 'models').is_dir():
        files['work/models'] = (stat.S_IFDIR | 0o755, b'')
        for plugin in (exe.parent / 'models').glob('*.so'):
            files['work/models/' + plugin.name] = (stat.S_IFREG | 0o755, plugin.read_bytes())
    # No host mounts, network, or hardware passthrough in this VM.
    command = ' '.join(shlex.quote(a) for a in ['/work/test', *args[1:]])
    script = ('#!/bin/sh\n'
              'mount -t proc proc /proc\nmount -t sysfs sysfs /sys\n'
              'mount -t devtmpfs devtmpfs /dev\nmkdir -p /tmp /run\n'
              'export PATH=/bin:/sbin:/usr/bin:/usr/sbin\n'
              'echo UGREENCTL_TEST_BEGIN\n' + command + '\n'
              'rc=$?\necho UGREENCTL_TEST_EXIT=$rc\npoweroff -f\n')
    files['init'] = (stat.S_IFREG | 0o755, script.encode())
    ordered = sorted(files.items(), key=lambda item: (item[0].count('/'), item[0]))
    data = b''.join(entry(name, *value, ino=i+1)
                    for i, (name, value) in enumerate(ordered))
    data += entry('TRAILER!!!', 0)
    with tempfile.TemporaryDirectory(prefix='ugreenctl-qemu-') as tmp:
        initrd = Path(tmp) / 'test.cpio.gz'
        initrd.write_bytes(gzip.compress(data, compresslevel=1))
        emulator = ('qemu-system-aarch64' if arm64 else 'qemu-system-x86_64') + ('.exe' if os.name == 'nt' else '')
        cmd = [str(runtime_root/'qemu'/emulator), '-machine', 'virt' if arm64 else 'q35',
               '-accel', 'tcg', '-m', '256', '-smp', '1', '-display', 'none',
               '-serial', 'stdio', '-monitor', 'none', '-nic', 'none', '-no-reboot',
               # QEMU's Windows file API may reject non-ASCII absolute names.
               # These paths are relative to the explicit runtime_root cwd.
               '-L', 'qemu/share', '-kernel', 'arm64/vmlinuz-virt' if arm64 else 'vmlinuz-virt', '-initrd', str(initrd),
               '-append', ('console=ttyAMA0' if arm64 else 'console=ttyS0') + ' rdinit=/init panic=-1 quiet']
        if arm64: cmd += ['-cpu', 'cortex-a72']
        if not arm64 and os.environ.get('UGREENCTL_TEST_VM_DMI'):
            cmd += ['-smbios', 'type=1,manufacturer=UGREEN,product=' +
                    os.environ['UGREENCTL_TEST_VM_DMI'] + ',version=EM_DXP2800_V1.0.24']
        try:
            completed = subprocess.run(cmd, cwd=runtime_root, stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT, timeout=timeout)
            output = completed.stdout.decode(errors='replace')
        except subprocess.TimeoutExpired as exc:
            print((exc.stdout or b'').decode('utf-8', errors='replace'))
            return 124
        if completed.returncode != 0:
            print(output)
            return 125
    normalized = output.replace('\r\n', '\n')
    matches = re.findall(r'^UGREENCTL_TEST_BEGIN\n(.*?)^UGREENCTL_TEST_EXIT=([0-9]+)\n', normalized, re.S | re.M)
    if (len(re.findall(r'^UGREENCTL_TEST_BEGIN$', normalized, re.M)) != 1
            or len(re.findall(r'^UGREENCTL_TEST_EXIT=[0-9]+$', normalized, re.M)) != 1
            or len(matches) != 1 or not 0 <= int(matches[0][1]) <= 255):
        print(output)
        return 125
    print(matches[0][0], end='')
    return int(matches[0][1])

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime-root', type=Path,
                        default=os.environ.get('UGREENCTL_TEST_RUNTIME_ROOT'),
                        help='Explicit external QEMU/kernel/rootfs/libc directory; alternatively set UGREENCTL_TEST_RUNTIME_ROOT')
    parser.add_argument('--timeout', type=int, default=45, help='VM timeout in seconds (default: 45)')
    parser.add_argument('executable', type=Path)
    parser.add_argument('arguments', nargs=argparse.REMAINDER)
    options = parser.parse_args()
    if options.runtime_root is None:
        parser.error('--runtime-root or UGREENCTL_TEST_RUNTIME_ROOT is required; no runtime is bundled')
    if options.timeout <= 0:
        parser.error('--timeout must be positive')
    try:
        return run([str(options.executable), *options.arguments], options.runtime_root, options.timeout)
    except (OSError, ValueError, tarfile.TarError) as error:
        print(f'QEMU runner: {error}', file=sys.stderr)
        return 125


if __name__ == '__main__':
    sys.exit(main())
