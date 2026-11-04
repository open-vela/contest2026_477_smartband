"""Sweep six Gemini S1 actions from the home page and collect real board counters."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', required=True)
    parser.add_argument('--adb-port', type=int, default=5037)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--ui-address', required=True)
    parser.add_argument('--lcd-address', required=True)
    parser.add_argument('--label', required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=20)
    args = parser.parse_args()
    scripts = Path(__file__).resolve().parent
    output = args.output_dir
    output.mkdir(parents=True, exist_ok=True)
    link = ['--adb', args.adb, '--adb-port', str(args.adb_port), '--serial', args.serial]

    def control(*command):
        subprocess.run([sys.executable, str(scripts/'mooncat_control.py'),
                        '--transport', 'adb', *link, *map(str, command)], check=True)

    def state():
        return json.loads(subprocess.check_output([sys.executable,
            str(scripts/'mooncat_control.py'), '--transport', 'adb', *link, 'state'], text=True))

    names = ['cat', 'shy-wave', 'phone-rest', 'balloon-rise', 'laugh', 'toilet-break']
    results = []
    for index, name in enumerate(names):
        control('swipe', 160, 35, 160, 200)
        control('tap', 160, 144)
        control('tap', 160, round((76 + 56 * index + 24) * 240 / 520))
        label = args.label+'-'+name
        control('screenshot', output/(label+'.png'))
        time.sleep(5)
        before = state()
        if before['page'] != 0 or before['overlay'] != 0 or before['action'] != index:
            raise RuntimeError('Requested animation is not active: '+repr(before))
        result = output/(label+'.json')
        subprocess.run([sys.executable, str(scripts/'collect_ui_profile.py'), *link,
                        '--ui-address', args.ui_address, '--lcd-address', args.lcd_address,
                        '--label', label, '--seconds', str(args.seconds),
                        '--output', str(result)], check=True)
        after = state()
        if any(after[key] != before[key] for key in ('page','overlay','action')):
            raise RuntimeError('Animation scene changed during sampling: '+repr(after))
        sample = json.loads(result.read_text())
        sample.update(ui_before=before, ui_after=after)
        result.write_text(json.dumps(sample, indent=2), encoding='utf-8')
        results.append(sample)
        (output/(args.label+'-all-actions.json')).write_text(
            json.dumps(results, indent=2), encoding='utf-8')
        print('SAMPLED', name, flush=True)
    control('swipe', 160, 35, 160, 200)
    control('tap', 160, 144)
    control('tap', 160, 46)


if __name__ == '__main__':
    main()
