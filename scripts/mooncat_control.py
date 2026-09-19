"""Control the Native UI over the existing, host-verified board SSH channel."""
import argparse
import io
import pathlib
import re
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default='172.20.10.8')
    parser.add_argument('--key', type=pathlib.Path,
                        default=pathlib.Path.home()/'.ssh/gemini_s1_wifi_ed25519_v2')
    parser.add_argument('--known-hosts', type=pathlib.Path,
                        default=pathlib.Path.home()/'.ssh/gemini_s1_closure_known_hosts')
    sub = parser.add_subparsers(dest='operation', required=True)
    screenshot = sub.add_parser('screenshot')
    screenshot.add_argument('output', type=pathlib.Path)
    tap = sub.add_parser('tap')
    tap.add_argument('x', type=int); tap.add_argument('y', type=int)
    swipe = sub.add_parser('swipe')
    for coordinate in ['x0', 'y0', 'x1', 'y1']:
        swipe.add_argument(coordinate, type=int)
    swipe.add_argument('--duration', type=int, default=400)
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9.:-]+', args.host) or args.host.startswith('-'):
        parser.error('invalid host')
    ssh = ['ssh', '-i', str(args.key), '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=5',
           '-o', 'IdentitiesOnly=yes', '-o', 'StrictHostKeyChecking=yes',
           '-o', 'UserKnownHostsFile='+str(args.known_hosts), 'codex@'+args.host]

    def read(command):
        # Board sshd needs time to drain stdout before the command exits.
        return subprocess.run(ssh+[command+'; usleep 200000'],
                              capture_output=True, timeout=15)

    request_id = time.time_ns() // 1000000 % 4000000000
    command = f'{request_id} {args.operation}'
    if args.operation == 'tap': command += f' {args.x} {args.y}'
    if args.operation == 'swipe':
        command += f' {args.x0} {args.y0} {args.x1} {args.y1} {args.duration}'
    result = read("echo '"+command+"' > /tmp/mooncat-control.req.part; "
                  'mv /tmp/mooncat-control.req.part /tmp/mooncat-control.req')
    if result.returncode:
        raise RuntimeError('SSH request failed: '+result.stderr.decode(errors='replace'))
    deadline = time.monotonic()+10
    while time.monotonic() < deadline:
        result = read('cat /tmp/mooncat-control.status')
        fields = result.stdout.decode(errors='replace').strip().split()
        if len(fields) == 3 and fields[0] == str(request_id):
            if int(fields[1]) != 0:
                raise RuntimeError('Board command failed, errno='+fields[1])
            break
        time.sleep(.1)
    else:
        raise TimeoutError('No matching UI-thread response; firmware may lack this interface')
    if args.operation == 'screenshot':
        from PIL import Image
        result = read('cat /tmp/mooncat-screen.ppm')
        header = re.match(rb'P6\n(\d+) (\d+)\n255\n', result.stdout)
        if result.returncode or not header:
            raise RuntimeError('Invalid screenshot transfer')
        expected = header.end()+int(header[1])*int(header[2])*3
        if len(result.stdout) != expected:
            raise RuntimeError(f'Truncated screenshot: {len(result.stdout)}/{expected} bytes')
        image = Image.open(io.BytesIO(result.stdout))
        image.load()
        args.output.parent.mkdir(parents=True, exist_ok=True)
        image.save(args.output, format='PNG')
        print(f'UI snapshot {image.width}x{image.height}: {args.output.resolve()}')
    print(f'PASS request={request_id} operation={args.operation} ui_tick={fields[2]}')


if __name__ == '__main__':
    main()
