"""Isolated packaging policy fixtures; never execute synthetic ELF files."""
import importlib.util
import io
import json
from pathlib import Path
import struct
import tarfile
import tempfile

REPO = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('packaging', REPO / 'scripts/package-almighty.py')
p = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p)
checks = 0


def refused(action, fragment):
    global checks
    try:
        action()
    except p.PackageError as exc:
        assert fragment in str(exc), str(exc)
    else:
        raise AssertionError('Expected refusal: ' + fragment)
    checks += 1


with tempfile.TemporaryDirectory(prefix='almighty-packaging-') as temporary:
    root = Path(temporary)
    for directory in p.SOURCE_DIRS:
        (root / directory).mkdir(parents=True)
        (root / directory / 'fixture.c').write_text('/* fixture */\n', encoding='utf-8')
    # Copy metadata and author scripts; production C/H are represented by fixtures.
    for name in sorted(p.source_inputs(REPO)):
        if Path(name).suffix in ('.c', '.h') and Path(name).parts[0] in p.SOURCE_DIRS:
            continue
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes((REPO / name).read_bytes())
    builds = {architecture: root / ('build-' + architecture) for architecture in p.ARCH_MACHINES}
    for architecture, build in builds.items():
        (build / 'models').mkdir(parents=True)
        (build / 'CMakeCache.txt').write_text('CMAKE_BUILD_TYPE:STRING=Release\nCMAKE_INSTALL_PREFIX:PATH=/usr/local\nCMAKE_INSTALL_LIBDIR:PATH=lib\nCMAKE_PROJECT_VERSION:STATIC=0.2.0\n', encoding='utf-8')
        (build / 'CTestTestfile.cmake').write_text(''.join(f'add_test("test-{index}" "fixture")\n' for index in range(23)), encoding='utf-8')
        for name in p.PROGRAMS + tuple('models/' + name + '.so' for name in p.PLUGINS):
            data = bytearray(120)
            data[:7] = b'\x7fELF\x02\x01\x01'
            struct.pack_into('<HHI', data, 16, 3 if name.endswith('.so') else 2, p.ARCH_MACHINES[architecture], 1)
            struct.pack_into('<H', data, 52, 64)
            struct.pack_into('<Q', data, 32, 64)
            struct.pack_into('<HH', data, 54, 56, 1)
            data += b'/usr/local/lib/ugreenctl/models\0'
            struct.pack_into('<IIQQQQQQ', data, 64, 1, 5, 0, 0x400000, 0x400000, len(data), len(data), 4096)
            (build / name).write_bytes(data)
    verification_path = root / 'verification.json'
    report = {'complete': False, 'target_count': 25, 'excluded_by_user': ['DH2100', 'DH2100+'],
              'physical_validation': 'not performed',
              'full_suite': {'x86_64': '23/23 passed', 'aarch64': {'status': 'passed', 'passed': 23, 'total': 23, 'failed': 0}}}
    report['full_suite_artifacts'] = {}
    for architecture in builds:
        log = root / 'firmware-inbox/work' / (architecture + '.log')
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text(''.join(f'{index+1}/23 Test #{index+1}: test-{index} ........ Passed 0.01 sec\n' for index in range(23))
                       + '100% tests passed, 0 tests failed out of 23\n', encoding='utf-8')
        report['full_suite_artifacts'][architecture] = {'path': log.relative_to(root).as_posix(), 'sha256': p.sha256(log.read_bytes())}

    def save(refresh=True):
        if refresh:
            report['source_sha256'] = {name: p.sha256((root / name).read_bytes()) for name in p.source_inputs(root)}
            report['binary_sha256'] = {path.relative_to(root).as_posix(): p.sha256(path.read_bytes())
                                      for build in builds.values() for path in build.rglob('*')
                                      if path.is_file() and path.name not in {'CMakeCache.txt', 'CTestTestfile.cmake'}}
        verification_path.write_bytes(p.json_bytes(report))

    save()
    preview = lambda: p.prepare(root, builds, verification_path, True)
    refused(lambda: p.prepare(root, builds, verification_path, False), 'verification.complete is not true')
    plans = preview()
    assert len(plans) == 2
    assert json.loads(plans[0]['manifest'])['status'] == 'experimental-incomplete'
    checks += 2
    data = p.archive_bytes(plans[0])
    assert data == p.archive_bytes(plans[0])
    with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as archive:
        members = archive.getmembers()
        assert all(member.isfile() and 'firmware-inbox' not in member.name and '..' not in member.name.split('/') for member in members)
        assert len([member for member in members if member.name.endswith('.so')]) == 9
        assert archive.extractfile(plans[0]['stem'] + '/manifest.json').read() == plans[0]['manifest']
        helper = archive.getmember(plans[0]['stem'] + '/usr/local/bin/ugreenctl-n76')
        assert helper.mode == 0o755
        for name in p.N76_RESOURCES:
            assert archive.extractfile(plans[0]['stem'] + '/usr/local/share/ugreenctl/n76/' + Path(name).name).read() == (REPO / name).read_bytes()
    checks += 4
    output = root / 'out'
    assert len(p.write_packages(plans, output)) == 5
    refused(lambda: p.write_packages(plans, output), 'Refusing to overwrite')
    checks += 1

    fixture = root / 'disk/fixture.c'
    fixture.write_text('changed', encoding='utf-8')
    refused(preview, 'Stale source SHA-256')
    save()
    key = 'build-x86_64/models/dxp2800.so'
    del report['binary_sha256'][key]
    save(False)
    refused(preview, 'Missing binary SHA-256')
    save()
    del report['source_sha256']['fan/fixture.c']
    save(False)
    refused(preview, 'Missing source/document SHA-256')
    save()

    binary = builds['aarch64'] / 'ugreenctl'
    original = binary.read_bytes()
    wrong = bytearray(original)
    struct.pack_into('<H', wrong, 18, 62)
    binary.write_bytes(wrong)
    save()
    refused(preview, 'Wrong ELF machine')
    binary.write_bytes(original)
    module = builds['x86_64'] / 'models/dxp2800.so'
    original_module = module.read_bytes()
    wrong = bytearray(original_module)
    struct.pack_into('<H', wrong, 16, 2)
    module.write_bytes(wrong)
    save()
    refused(preview, 'Wrong ELF type')
    module.write_bytes(original_module)
    extra = builds['x86_64'] / 'models/foreign.so'
    extra.write_bytes(original_module)
    save()
    refused(preview, 'Unexpected/missing model .so set')
    extra.unlink()
    save()

    cache = builds['x86_64'] / 'CMakeCache.txt'
    original_cache = cache.read_text(encoding='utf-8')
    cache.write_text(original_cache.replace('/usr/local', 'C:/Program Files/ugreenctl'), encoding='utf-8')
    refused(preview, 'Invalid Linux install prefix')
    cache.write_text(original_cache.replace('/usr/local', '/opt/ugreenctl'), encoding='utf-8')
    refused(preview, 'does not embed')
    cache.write_text(original_cache.replace('Release', 'Debug'), encoding='utf-8')
    refused(preview, 'must be Release')
    cache.write_text(original_cache, encoding='utf-8')

    report['full_suite']['x86_64'] = '23/24 passed'
    save()
    refused(preview, '23/23 passed full suite')
    report['full_suite']['x86_64'] = '23/23 passed'
    report['full_suite']['aarch64']['failed'] = 1
    save()
    refused(preview, 'Invalid structured full-suite')
    report['full_suite']['aarch64']['failed'] = 0
    log_record = report['full_suite_artifacts']['x86_64']
    log = root / log_record['path']
    original_log = log.read_bytes()
    log.write_bytes(original_log.replace(b'100% tests passed', b'0% tests passed'))
    save()
    refused(preview, 'Stale full-suite log SHA-256')
    log_record['sha256'] = p.sha256(log.read_bytes())
    save()
    refused(preview, 'does not prove all configured tests passed')
    log.write_bytes(original_log.replace(b'test-22', b'other-test'))
    log_record['sha256'] = p.sha256(log.read_bytes())
    save()
    refused(preview, 'does not prove all configured tests passed')
    log.write_bytes(original_log)
    log_record['sha256'] = p.sha256(original_log)
    original_arm_log = report['full_suite_artifacts']['aarch64']
    report['full_suite_artifacts']['aarch64'] = dict(log_record)
    save()
    refused(preview, 'must use distinct full-suite log paths')
    report['full_suite_artifacts']['aarch64'] = original_arm_log

    report['complete'] = True
    save()
    assert json.loads(preview()[0]['manifest'])['scope_complete'] is False
    checks += 1
    refused(lambda: p.prepare(root, builds, verification_path, False), 'Compatibility integration is still incomplete')
    compatibility_path = root / 'models/compatibility.json'
    catalog = json.loads(compatibility_path.read_text(encoding='utf-8'))
    integration = catalog['builtin_capabilities']['all_model_integration']
    integration['state'] = 'complete'
    compatibility_path.write_bytes(p.json_bytes(catalog))
    save()
    refused(lambda: p.prepare(root, builds, verification_path, False), 'Open delivery work remains')
    for model in integration['per_model']:
        if model['scope'] == 'target':
            model['remaining'] = []
            model['led'] = 'implemented-experimental'
            for capability in ('sata_physical_slots', 'nvme_physical_slots'):
                if model[capability].startswith('unknown'):
                    model[capability] = 'implemented-conditional'
    catalog['builtin_capabilities']['disk_mapping']['unknown_m2_physical_slot_models'] = []
    compatibility_path.write_bytes(p.json_bytes(catalog))
    save()
    # Only this synthetic catalog resolves every gap. Production state is never changed.
    gt = next(model for model in integration['per_model'] if model['product_name'] == 'DXP2800 GT')
    gt['sata_physical_slots'] = 'unknown-bios-routing'
    compatibility_path.write_bytes(p.json_bytes(catalog))
    save()
    refused(lambda: p.prepare(root, builds, verification_path, False), 'sata_physical_slots remains incomplete: DXP2800 GT')
    gt['sata_physical_slots'] = 'implemented-conditional'
    compatibility_path.write_bytes(p.json_bytes(catalog))
    save()
    completed = p.prepare(root, builds, verification_path, False)
    assert json.loads(completed[0]['manifest'])['status'] == 'complete-local'
    checks += 1

print(f'Packaging policy fixtures passed: {checks} checks')
