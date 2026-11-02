"""Offline rejection checks for the restricted kernel update path."""
import importlib.util
from pathlib import Path
import struct

import pytest

spec = importlib.util.spec_from_file_location('gemini_update',
    Path(__file__).resolve().parents[1]/'scripts/gemini_update.py')
update = importlib.util.module_from_spec(spec)
spec.loader.exec_module(update)


def image(path, kernel=b'\x00\x00\x00\x00rtos', resource=b'resource', corrupt=False):
    checksum = sum(struct.unpack('<2I', kernel)) & 0xffffffff
    items = {'nsh.fex': kernel, 'Vnsh.fex': struct.pack('<I', checksum+int(corrupt)),
             'boot0_nand.fex': b'\x00\x01\x09\x07\x5a\xa5\xa5\x5a',
             'res.fex': resource}
    items.update({f'item{i}': b'x' for i in range(18)})
    data = bytearray(23*1024)
    data[:8] = b'IMAGEWTY'
    struct.pack_into('<I', data, 8, 0x300)
    struct.pack_into('<I', data, 60, 22)
    for i, (name, payload) in enumerate(items.items(), 1):
        offset = len(data)
        data[i*1024+36:i*1024+36+len(name)] = name.encode()
        struct.pack_into('<QQQ', data, i*1024+292, 1024, len(payload), offset)
        data.extend(payload.ljust(1024, b'\0'))
    struct.pack_into('<Q', data, 24, len(data))
    path.write_bytes(data)


def test_only_valid_kernel_changes_are_accepted(tmp_path):
    old, new = tmp_path/'old.img', tmp_path/'new.img'
    image(old)
    image(new, kernel=b'\x01\x00\x00\x00rtos')
    assert update.audit_image(new, old)['changed_payloads'] == ['nsh.fex', 'Vnsh.fex']
    image(new, resource=b'changed')
    with pytest.raises(ValueError, match='only accepts'):
        update.audit_image(new, old)
    image(new, corrupt=True)
    with pytest.raises(ValueError, match='checksum'):
        update.audit_image(new, old)
    data = bytearray(old.read_bytes())
    struct.pack_into('<Q', data, 1024+308, 0)
    new.write_bytes(data)
    with pytest.raises(ValueError, match='Invalid image entry'):
        update.audit_image(new, old)
    data = bytearray(old.read_bytes())
    data[1024+8] ^= 1  # Changing a flash item tag must not pass a payload-only audit.
    new.write_bytes(data)
    with pytest.raises(ValueError, match='identity changed'):
        update.audit_image(new, old)


def test_backup_comparison_detects_missing_and_different_files(tmp_path):
    before, after = tmp_path/'before', tmp_path/'after'
    before.mkdir(); after.mkdir()
    (before/'config').write_bytes(b'private')
    assert update.compare_backup(before, after)['different_or_missing'] == ['config']
    (after/'config').write_bytes(b'wrong')
    assert update.compare_backup(before, after)['different_or_missing'] == ['config']
    (after/'config').write_bytes(b'private')
    assert update.compare_backup(before, after) == {'compared_files': 1, 'different_or_missing': []}
