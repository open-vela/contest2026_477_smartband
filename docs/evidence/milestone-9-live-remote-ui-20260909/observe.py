import argparse, os, pathlib, subprocess, sys, time

root = pathlib.Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--name', default='round2')
parser.add_argument('--prompt', default='20秒后主动提醒我休息')
parser.add_argument('--frames', type=int, default=32)
parser.add_argument('--repeat', action='store_true')
args = parser.parse_args()
out = pathlib.Path(__file__).resolve().parent/args.name
out.mkdir(exist_ok=True)
env = dict(os.environ, PYTHONIOENCODING='utf-8')
capture_env = env.copy()
capture_env.pop('PYTHONPATH', None)
env['PYTHONPATH'] = r'C:\Users\Lenovo\AppData\Local\Packages\PythonSoftwareFoundation.Python.3.11_qbz5n2kfra8p0\LocalCache\local-packages\Python311\site-packages'
control = root/'contest2026_477_smartband/scripts/mooncat_control.py'
with (out/'observation.log').open('w', encoding='utf-8') as log:
    preflight = subprocess.run([sys.executable, str(control), 'screenshot', str(out/'preflight.png')], env=capture_env, capture_output=True, timeout=25)
    log.write(preflight.stdout.decode('utf-8', errors='replace')+preflight.stderr.decode('utf-8', errors='replace')); log.flush()
    if preflight.returncode: raise SystemExit('Preflight capture failed; request not sent')
    trial = subprocess.Popen([sys.executable, str(root/'work/openvela-closure-20260908/ws_trial.py'), '--prompt', args.prompt, '--seconds', str(max(10,args.frames*3)), '--output', str(out/'ws-trial.txt')]+(['--repeat'] if args.repeat else []), env=env, stdout=log, stderr=subprocess.STDOUT)
    start = time.monotonic()
    for index in range(args.frames):
        elapsed = time.monotonic()-start
        log.write(f'CAPTURE {index} elapsed={elapsed:.2f}\n'); log.flush()
        try:
            r = subprocess.run([sys.executable, str(control), 'screenshot', str(out/f'trial-{index:02d}.png')], env=capture_env, capture_output=True, timeout=25)
            log.write(r.stdout.decode('utf-8', errors='replace')+r.stderr.decode('utf-8', errors='replace')); log.flush()
            if r.returncode: break
        except subprocess.TimeoutExpired:
            log.write('CAPTURE TIMEOUT\n'); log.flush(); break
        time.sleep(1)
    trial.wait(timeout=75)
print('Observation complete', flush=True)
