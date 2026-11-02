"""Read existing UIF6/LCF6 counters over ADB, without resetting them.

Addresses MUST be resolved from the running firmware's ELF or its bound raw
payload, not another build. Reads are limited to the Gemini S1 firmware RAM
range. No memory writes are issued. Counters are sequential snapshots, not
atomic; use long quiet windows and keep screenshots outside those windows.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess

UI_FIELDS = ('magic motion_ticks visual_frames nochange_ticks missed_ticks '
             'late_ticks motion_elapsed_ms prepare_count prepare_time_us '
             'compose_time_us changed_pixels dirty_pixels last_changed_pixels '
             'last_dirty_pixels decode_errors composite_buffer_bytes').split()
LCD_FIELDS = ('magic render_count refresh_time_us flush_count flush_pixels '
              'putarea_time_us last_flush_pixels max_flush_pixels ioctl_errors '
              'buffer_bytes buffer_rows render_mode').split()


def parse_snapshot(text, ui_address, lcd_address):
    words = {int(a, 16): int(v, 16) for a, v in re.findall(
        r'0x([0-9a-fA-F]+)\s*=\s*0x([0-9a-fA-F]+)', text)}
    result = {}
    for key, address, fields, magic in (
        ('ui', ui_address, UI_FIELDS, 0x55494636),
        ('lcd', lcd_address, LCD_FIELDS, 0x4c434636),
    ):
        result[key] = {name: words[address + i * 4] for i, name in enumerate(fields)}
        if result[key]['magic'] != magic:
            raise ValueError('Counter identity mismatch; verify running firmware addresses')
    uptime = re.search(r'UPTIME\s*\n\s*(\d+(?:\.\d+)?)', text)
    if not uptime:
        raise ValueError('Missing board uptime')
    result['uptime_s'] = float(uptime[1])
    return result


def summarize(start, end):
    seconds = end['uptime_s'] - start['uptime_s']
    if seconds <= 0:
        raise ValueError('Nonpositive sample window')
    delta = {k: {f: (end[k][f] - start[k][f]) & 0xffffffff
                 for f in fields if f != 'magic'}
             for k, fields in [('ui', UI_FIELDS), ('lcd', LCD_FIELDS)]}
    ui, lcd = delta['ui'], delta['lcd']
    per = lambda n, d: n / d if d else None
    return dict(seconds=seconds,
        motion_fps=ui['motion_ticks'] / seconds,
        visual_fps=ui['visual_frames'] / seconds,
        render_fps=lcd['render_count'] / seconds,
        flush_calls_per_s=lcd['flush_count'] / seconds,
        late_ticks=ui['late_ticks'], missed_ticks=ui['missed_ticks'],
        prepare_mean_us=per(ui['prepare_time_us'], ui['prepare_count']),
        compose_mean_us=per(ui['compose_time_us'], ui['motion_ticks']),
        refresh_mean_us=per(lcd['refresh_time_us'], lcd['render_count']),
        putarea_mean_us=per(lcd['putarea_time_us'], lcd['flush_count']),
        flush_pixels_per_s=lcd['flush_pixels'] / seconds,
        dirty_pixels_per_visual=per(ui['dirty_pixels'], ui['visual_frames']),
        ioctl_errors=lcd['ioctl_errors'], decode_errors=ui['decode_errors'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', default='adb')
    parser.add_argument('--adb-port', type=int, default=5037)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--ui-address', required=True, type=lambda x: int(x, 16))
    parser.add_argument('--lcd-address', required=True, type=lambda x: int(x, 16))
    parser.add_argument('--seconds', type=int, default=20)
    parser.add_argument('--label', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not 2 <= args.seconds <= 50:
        parser.error('seconds must be 2..50')
    if not 1 <= args.adb_port <= 65535 or args.serial.startswith('-'):
        parser.error('invalid ADB port or serial')
    for address, length in [(args.ui_address, 64), (args.lcd_address, 48)]:
        if address % 4 or not 0x41400000 <= address <= 0x42200000 - length:
            parser.error('counter address outside firmware RAM range or unaligned')
    read = (f'echo UPTIME; cat /proc/uptime; mw {args.ui_address:x} 40; '
            f'mw {args.lcd_address:x} 30')
    command = f'{read}; echo BETWEEN; sleep {args.seconds}; {read}; usleep 200000'
    run = subprocess.run([args.adb, '-P', str(args.adb_port), '-s', args.serial,
                          'shell', command], capture_output=True, timeout=args.seconds+15)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.with_suffix('.raw.txt').write_bytes(run.stdout + run.stderr)
    if run.returncode:
        raise RuntimeError('ADB sampling failed; see raw log')
    text = run.stdout.decode(errors='replace').replace('\r\n', '\n')
    halves = text.split('BETWEEN\n')
    if len(halves) != 2:
        raise ValueError('Missing sample delimiter')
    start, end = [parse_snapshot(part, args.ui_address, args.lcd_address) for part in halves]
    result = dict(label=args.label, summary=summarize(start, end), start=start, end=end,
                  ui_address=hex(args.ui_address), lcd_address=hex(args.lcd_address),
                  evidence='On-device counters; not optical panel frame measurement')
    args.output.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result['summary'], indent=2))


if __name__ == '__main__':
    main()
