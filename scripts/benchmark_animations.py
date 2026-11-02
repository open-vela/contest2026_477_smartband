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

    names = ['cat', 'shy-wave', 'phone-rest', 'balloon-rise', 'laugh', 'toilet-break']
    results = []
    for index, name in enumerate(names):
        control('swipe', 160, 35, 160, 200)
        control('tap', 160, 144)
        control('tap', 160, round((76 + 56 * index + 24) * 240 / 520))
        label = args.label+'-'+name
        control('screenshot', output/(label+'.png'))
        time.sleep(5)
        result = output/(label+'.json')
        subprocess.run([sys.executable, str(scripts/'collect_ui_profile.py'), *link,
                        '--ui-address', args.ui_address, '--lcd-address', args.lcd_address,
                        '--label', label, '--seconds', str(args.seconds),
                        '--output', str(result)], check=True)
        results.append(json.loads(result.read_text()))
        (output/(args.label+'-all-actions.json')).write_text(
            json.dumps(results, indent=2), encoding='utf-8')
        print('SAMPLED', name, flush=True)
    control('swipe', 160, 35, 160, 200)
    control('tap', 160, 144)
    control('tap', 160, 46)


if __name__ == '__main__':
    main()
