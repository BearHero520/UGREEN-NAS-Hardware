"""Reproduce the locally pinned DXP6800 Pro-parameter extension."""
import importlib.util
from pathlib import Path
root = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('extension', root / 'scripts/prepare-llled-dxp6800.py')
extension = importlib.util.module_from_spec(spec)
spec.loader.exec_module(extension)
extension.BASE = root / 'artifacts/Almighty/2026-10-27-wake'
extension.BASE_SHA = 'b9a6c86fa641d5b1e66ccd7b7edd06e36a27953e29ecfcedc5c27de4aab261de'
extension.OUT = Path(__file__).resolve().parent
extension.main()
