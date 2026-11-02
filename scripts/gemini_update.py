"""Background Gemini S1 update through OpenixCLI 0.2.4, with /data restoration.

Gemini's NAND firmware reinitializes /data even with OpenixCLI's partition mode.
This workflow therefore requires a fresh private backup, restores it after the
update, and compares every backed-up file byte for byte. It never uses raw NAND
writes. The RTC FEL address below is specific to R528 and the verified boot0.
"""
import argparse
import array
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import time


def image_payloads(path):
    data = Path(path).read_bytes()
    if data[:8] != b'IMAGEWTY' or struct.unpack_from('<I', data, 8)[0] != 0x300:
        raise ValueError('Expected unencrypted IMAGEWTY v3')
    if struct.unpack_from('<Q', data, 24)[0] != len(data):
        raise ValueError('Image size mismatch')
    count = struct.unpack_from('<I', data, 60)[0]
    if count != 22:
        raise ValueError('Expected the verified 22-item Gemini layout')
    result = {}
    end = (count + 1) * 1024
    for index in range(count):
        header = data[(index+1)*1024:(index+2)*1024]
        name = header[36:292].split(b'\0')[0].decode('ascii')
        stored, size, offset = struct.unpack_from('<QQQ', header, 292)
        if (name in result or '/' in name or '\\' in name or offset < end or
                offset % 1024 or size > stored or offset+stored > len(data)):
            raise ValueError('Invalid image entry: '+name)
        result[name] = data[offset:offset+size]
        end = offset+stored
    return result


def audit_image(image, reference):
    old, new = image_payloads(reference), image_payloads(image)
    def identity(path):
        with Path(path).open('rb') as stream:
            header = stream.read(96)
            entries = []
            for index in range(22):
                stream.seek((index+1)*1024)
                entries.append(stream.read(292))
        # Total image size and per-file offsets/lengths may change. Device IDs,
        # item type tags, attributes and ordered filenames must not change.
        return header[:24]+header[32:], entries
    if identity(reference) != identity(image):
        raise ValueError('Image device or item identity changed')
    changed = [name for name in old if old[name] != new.get(name)]
    if old.keys() != new.keys() or not set(changed) <= {'nsh.fex', 'Vnsh.fex'}:
        raise ValueError('This workflow only accepts a system-kernel update')
    kernel = new['nsh.fex']
    if not 0 < len(kernel) <= 16*1024*1024 or len(kernel) % 4 or kernel[4:8] != b'rtos':
        raise ValueError('Invalid Gemini kernel size or header')
    words = array.array('I'); words.frombytes(kernel)
    if sys.byteorder != 'little':
        words.byteswap()
    if new['Vnsh.fex'] != struct.pack('<I', sum(words) & 0xffffffff):
        raise ValueError('Invalid kernel checksum')
    boot0 = new['boot0_nand.fex']
    if b'\x00\x01\x09\x07' not in boot0 or b'\x5a\xa5\xa5\x5a' not in boot0:
        raise ValueError('Expected R528 RTC FEL constants missing')
    return {'kernel_bytes': len(kernel), 'changed_payloads': changed,
            'flash_mode': 'partition', 'partition': 'bootloader',
            'data_policy': 'backup, restore, byte-compare, reboot'}


def compare_backup(before, after):
    files = [p for p in before.rglob('*') if p.is_file()]
    different = [str(p.relative_to(before)) for p in files
                 if not (after/p.relative_to(before)).is_file()
                 or p.read_bytes() != (after/p.relative_to(before)).read_bytes()]
    return {'compared_files': len(files), 'different_or_missing': different}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', required=True)
    parser.add_argument('--adb-port', type=int, default=5037)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--openixcli', required=True)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--execute', action='store_true', help='Run the audited update; default is image audit only')
    args = parser.parse_args()
    if args.serial.startswith('-') or not 1 <= args.adb_port <= 65535:
        parser.error('Invalid ADB selector')
    report = audit_image(args.image, args.reference)
    print(json.dumps(report, indent=2), flush=True)
    if not args.execute:
        return
    work = args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    before, after = work/'private-before', work/'private-after'
    if before.exists() or after.exists():
        raise ValueError('Refusing to overwrite an existing private backup')
    adb = [args.adb, '-P', str(args.adb_port), '-s', args.serial]

    def run(command, log=None, timeout=40):
        if log:
            with (work/log).open('wb') as stream:
                return subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT,
                                      check=True, timeout=timeout)
        return subprocess.check_output(command, stderr=subprocess.STDOUT, timeout=timeout).decode(errors='replace')

    def shell(command):
        return run(adb+['shell', command+'; usleep 200000'])

    def wait_adb():
        deadline = time.monotonic()+90
        while time.monotonic() < deadline:
            try:
                state = run(adb+['shell',
                    'uname -a; echo UI; pidof openvela_ui; echo AGENT; pidof ai_agent; usleep 200000'], timeout=5)
                if ('NuttX' in state and re.search(r'UI\s+\d+', state)
                        and re.search(r'AGENT\s+\d+', state)):
                    return
            except (subprocess.SubprocessError, OSError):
                pass
            time.sleep(1)
        raise TimeoutError('ADB did not reconnect; backup and logs retained')

    def stop_data_writers(label):
        shell('ps > /tmp/mooncat-update-ps.txt')
        run(adb+['pull', '/tmp/mooncat-update-ps.txt', str(work/(label+'-ps.txt'))])
        for line in (work/(label+'-ps.txt')).read_text().splitlines():
            fields = line.split()
            if (len(fields) >= 3 and fields[0].isdigit() and fields[0] == fields[1]
                    and 'Task' in fields and fields[-1] in ('openvela_ui', 'ai_agent', 'kvdbd')):
                shell('kill -15 '+fields[0])

    if run([args.openixcli, '--version']).strip() != 'openixcli 0.2.4':
        raise ValueError('Only OpenixCLI 0.2.4 has been verified for this workflow')
    report['before_firmware'] = shell('uname -a').strip()
    if 'NuttX' not in report['before_firmware']:
        raise ValueError('Expected the connected Gemini NuttX board')
    stop_data_writers('before')
    run(adb+['pull', '/data', str(before)], 'backup.log', 180)
    if not (before/'openvela_ui/ui-font.ttf').is_file():
        raise ValueError('Backup missing UI assets; no flash attempted')
    print('Private backup completed; entering FEL', flush=True)
    rtc = shell('mw 07090108')
    if not re.search(r'=\s*0x00000000', rtc):
        raise ValueError('Unexpected RTC FEL state')
    flag = shell('mw 07090108=5aa5a55a; mw 07090108')
    if '0x5aa5a55a' not in flag.lower():
        raise ValueError('FEL flag did not read back')
    run(adb+['reboot'])
    deadline = time.monotonic()+40
    location = []
    while time.monotonic() < deadline:
        try:
            scan = run([args.openixcli, '--backend', 'winusb', 'scan'])
            location = re.findall(r'Bus\s+(\d+),\s*Port\s+(\d+)', scan)
            if location:
                break
        except subprocess.SubprocessError:
            pass
        time.sleep(1)
    if len(location) != 1:
        raise ValueError('Expected exactly one FEL device; no flash attempted')
    bus, port = location[0]
    command = [args.openixcli, '--backend', 'winusb', 'flash', str(args.image.resolve()),
               '--bus', bus, '--port', port, '--mode', 'partition', '--partitions',
               'bootloader', '--verify', 'true', '--post-action', 'reboot', '--verbose']
    print('Flashing system partition through FES', flush=True)
    # Do not impose a host timeout that could terminate an active NAND write.
    run(command, 'flash.log', None)
    wait_adb()
    stop_data_writers('after')
    print('Restoring /data and comparing every backed-up file', flush=True)
    run(adb+['push', str(before)+'/.' , '/data/'], 'restore.log', 180)
    run(adb+['pull', '/data', str(after)], 'readback.log', 180)
    report.update(compare_backup(before, after))
    (work/'result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    if report['different_or_missing']:
        raise ValueError('Restoration mismatch; see result.json; backup retained')
    run(adb+['reboot'])
    wait_adb()
    report['after_firmware'] = shell('uname -a').strip()
    (work/'result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2), flush=True)


if __name__ == '__main__':
    main()
