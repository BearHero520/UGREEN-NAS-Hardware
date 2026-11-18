"""Safety checks for the standalone read-only intake probe (no hardware IO)."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import sys
from unittest.mock import patch, Mock

if sys.platform == 'win32':
    # These tests never enter main or acquire a real lock.
    sys.modules['fcntl'] = Mock()

spec = importlib.util.spec_from_file_location(
    'probe', Path(__file__).resolve().parents[1] / 'scripts/probe_idx6012_ec.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class ProbeSafety(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)
        self.addCleanup(patch.stopall)
        patch.object(probe, 'Path', side_effect=lambda p: self.root / p.lstrip('/')).start()
        patch.object(probe.os.path, 'lexists', side_effect=lambda p: (self.root / p.lstrip('/')).exists()).start()
        self.put('/sys/class/dmi/id/sys_vendor', 'UGREEN')
        self.put('/sys/class/dmi/id/product_name', 'Default string')
        self.put('/sys/class/dmi/id/product_version', 'iDX6011_iDX6012 V3.0 241112')
        self.put('/sys/class/dmi/id/bios_version', '5.32')
        self.put('/proc/ioports', '0000-0cf7 : PCI Bus 0000:00\n0064-0064 : keyboard\n')

    def put(self, p, value):
        dest = self.root / p.lstrip('/')
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(value)

    def test_confirmed_oem_board(self):
        self.assertEqual(probe.identity()['bios_version'], '5.32')
        probe.owners()

    def test_generic_dmi_is_not_an_allowlist(self):
        self.put('/sys/class/dmi/id/product_version', 'other board')
        with self.assertRaises(RuntimeError):
            probe.identity()

    def test_vendor_is_required(self):
        self.put('/sys/class/dmi/id/sys_vendor', 'another vendor')
        with self.assertRaises(RuntimeError):
            probe.identity()

    def test_active_kernel_ec_refused(self):
        self.put('/sys/bus/acpi/devices/fixture/status', '15')
        device = self.root / 'sys/bus/acpi/devices/fixture'
        with patch.object(Path, 'glob', return_value=iter([device])):
            with self.assertRaises(RuntimeError):
                probe.owners()

    def test_port_owner_refused(self):
        self.put('/proc/ioports', '0062-0062 : EC data\n')
        with self.assertRaises(RuntimeError):
            probe.owners()

    def test_redacted_ownership_refused(self):
        self.put('/proc/ioports', '0000-0000 : PCI Bus 0000:00\n')
        with self.assertRaises(RuntimeError):
            probe.owners()

    def test_stock_driver_refused(self):
        self.put('/sys/module/ug_idx6011_sio', '')
        with self.assertRaises(RuntimeError):
            probe.owners()


if __name__ == '__main__':
    unittest.main()
