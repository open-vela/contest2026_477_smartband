"""One-click, state-checked Gemini S1 demonstration over ADB (no firmware change)."""
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
from pathlib import Path
import queue
import shutil
import socket
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
PAGE_NAMES = ['首页', '天气', '运动', '健康', '音乐', '消息']
ACTION_NAMES = ['猫咪陪伴', '挥手', '放下手机', '气球', '大笑', '如厕提醒']
APP_FIELDS = 'version next_id last_now focus workout dnd pause_until focus_seconds shown_count completed_count'.split()
TASK_FIELDS = 'id revision request_id due duration phase reason recovery_end last_op last_revision requested_delay'.split()


class Stopped(Exception):
    pass


def parse_app(text):
    lines = text.strip().splitlines()
    if not lines or lines[0] != 'MCAPP2':
        raise RuntimeError('板子应用状态格式不匹配，请使用 11 月决赛固件。')
    values = list(map(int, lines[2:]))
    if len(values) != 98:
        raise RuntimeError('应用状态读取不完整，请检查 USB 连接。')
    state = dict(zip(APP_FIELDS, values[:10]))
    state['tasks'] = [dict(zip(TASK_FIELDS, values[10+i*11:21+i*11])) for i in range(8)]
    return state


def find_adb(explicit=None):
    candidates = [explicit, os.environ.get('MOONCAT_ADB'), shutil.which('adb'),
                  ROOT.parent/'_codex_android_toolchain_smart_band/sdk/platform-tools/adb.exe']
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return str(Path(candidate).resolve())
    raise RuntimeError('找不到 adb。请设置 MOONCAT_ADB 为 adb.exe 的完整路径。')


class Board:
    def __init__(self, adb, port, serial=None):
        self.adb, self.port, self.serial = adb, port, serial
        self.env = dict(os.environ, ADB_LIBUSB='1')
        self.request_id = time.time_ns() // 1000000 % 4000000000

    def run(self, args, timeout=12):
        result = subprocess.run([self.adb, '-P', str(self.port), *args],
                                capture_output=True, timeout=timeout, env=self.env,
                                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        if result.returncode:
            detail = result.stderr.decode('utf-8', errors='replace').strip()
            raise RuntimeError('板子连接失败：'+detail[:250])
        return result.stdout.decode('utf-8', errors='replace').strip()

    def connect(self):
        devices = [line.split()[0] for line in self.run(['devices']).splitlines()
                   if len(line.split()) == 2 and line.split()[1] == 'device']
        if self.serial:
            if self.serial not in devices:
                raise RuntimeError(f'未连接指定设备 {self.serial}，请插好 USB 后重试。')
        elif len(devices) == 1:
            self.serial = devices[0]
        else:
            raise RuntimeError('请只连接一块板子，或使用 --serial 指定设备。')
        return self.state()

    def shell(self, command):
        return self.run(['-s', self.serial, 'shell', command+'; usleep 100000'])

    def control(self, operation, *values):
        self.request_id = (self.request_id + 1) % 4000000000
        request = f'{self.request_id} {operation}' + ''.join(f' {int(v)}' for v in values)
        self.shell(f"echo '{request}' > /tmp/mooncat-control.req.part; "
                   'mv /tmp/mooncat-control.req.part /tmp/mooncat-control.req')
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            fields = self.shell('cat /tmp/mooncat-control.status').split()
            if len(fields) == 3 and fields[0] == str(self.request_id):
                if fields[1] != '0':
                    raise RuntimeError(f'板子未执行 {operation}，错误码 {fields[1]}。')
                return
            time.sleep(.08)
        raise RuntimeError('板子界面未回应。请关闭其他控制程序并检查连接。')

    def state(self):
        self.control('state')
        return json.loads(self.shell('cat /tmp/mooncat-ui-state.json'))

    def app(self):
        return parse_app(self.shell('cat /data/etc/mooncat/app-state-v1.txt'))

    def sync_clock(self):
        now = dt.datetime.now()
        month = 'Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec'.split()[now.month-1]
        self.shell(f"date -s '{month} {now.day:02d} {now:%H:%M:%S} {now.year}'")

    def tap(self, x, y):
        self.control('tap', x, y)
        time.sleep(.3)

    def swipe(self, x0=235, y0=120, x1=85, y1=120):
        self.control('swipe', x0, y0, x1, y1, 600)
        time.sleep(.4)

    def up(self):
        self.swipe(160, 190, 160, 45)

    def down(self):
        self.swipe(160, 45, 160, 190)

    def capture(self, path):
        # Screenshot conversion stays in the existing verified utility.
        subprocess.run([sys.executable, str(ROOT/'scripts/mooncat_control.py'),
                        '--transport', 'adb', '--adb', self.adb, '--adb-port', str(self.port),
                        '--serial', self.serial, 'screenshot', str(path)],
                       check=True, capture_output=True, timeout=30, env=self.env,
                       creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))


class Demo:
    def __init__(self, board, stop, report, dwell=4, capture=None):
        self.b, self.stop, self.report = board, stop, report
        self.dwell, self.capture = dwell, Path(capture) if capture else None
        self.panel = False
        self.owns_workout = False
        self.task_id = None
        self.initial_action = None
        self.prepared = False
        self.sequence = 0

    def check_stop(self):
        if self.stop.is_set():
            raise Stopped()

    def wait(self, seconds):
        if self.stop.wait(max(0, seconds)):
            raise Stopped()

    def expect(self, **expected):
        state = self.b.state()
        if any(state.get(k) != v for k, v in expected.items()):
            raise RuntimeError('页面切换未到预期位置，请关闭弹窗后重新开始。'
                               f' 预期 {expected}，实际 {state}')
        return state

    def show(self, title, seconds=None, **expected):
        self.check_stop()
        state = self.expect(**expected) if expected else self.b.state()
        self.sequence += 1
        self.report(title, state)
        if self.capture:
            self.capture.mkdir(parents=True, exist_ok=True)
            self.b.capture(self.capture/f'{self.sequence:02d}.png')
        self.wait(self.dwell if seconds is None else seconds)

    def go(self, page):
        self.check_stop()
        state = self.expect(overlay=0)
        for _ in range(6):
            if state['page'] == page:
                return
            self.check_stop()
            self.b.swipe()
            state = self.expect(overlay=0)
        raise RuntimeError('无法切换到'+PAGE_NAMES[page])

    def close_overlay(self):
        for _ in range(4):
            state = self.b.state()
            if not state['overlay']:
                return
            if state['workout']:
                raise RuntimeError('当前有运动正在进行，请先结束运动。')
            if state['health_measuring']:
                time.sleep(3)
            self.b.up()
        self.expect(overlay=0)

    def select_action(self, action):
        self.go(0)
        self.b.down()
        self.expect(overlay=1)
        self.b.tap(160, 145)
        self.expect(overlay=3)
        self.b.tap(160, round((100+action*56)*.465))
        self.expect(overlay=0, action=action)

    def finish_workout(self):
        state = self.b.state()
        if state['overlay'] != 7:
            if state['workout']:
                raise RuntimeError('运动界面状态异常，请在板子上结束运动。')
            self.owns_workout = False
            return
        if not state['workout']:
            self.b.up()
            self.owns_workout = False
            return
        for _ in range(6):
            if state['sport_page'] == 5:
                break
            self.b.swipe(235, 50, 85, 50)
            state = self.b.state()
        self.expect(sport_page=5, workout=True)
        self.b.tap(160, 205)
        self.b.tap(200, 152)
        self.expect(page=0, overlay=0, workout=False)
        self.owns_workout = False

    def task(self):
        return next((t for t in self.b.app()['tasks'] if t['id'] == self.task_id), None)

    def wait_phase(self, phase, timeout):
        deadline = time.monotonic()+timeout
        while time.monotonic() < deadline:
            self.check_stop()
            task = self.task()
            if task and task['phase'] == phase:
                return task
            self.wait(.5)
        raise RuntimeError('提醒状态未按预期变化，请查看板子提示。')

    def reminder(self):
        self.go(0)
        before = self.b.app()
        if any(1 <= t['phase'] <= 4 for t in before['tasks']):
            raise RuntimeError('有其他提醒正在进行，请完成后再演示。')
        self.b.tap(230, 225)
        self.panel = True
        self.show('Rest · 专注、免打扰与恢复提醒')
        if before['dnd'] or before['pause_until'] > before['last_now']:
            self.report('当前处于免打扰或暂停状态，本轮保留设置，仅展示面板。', {})
            self.b.tap(160, 192)
            self.panel = False
            return
        self.task_id = before['next_id']
        self.b.tap(247, 148)
        self.wait_phase(1, 5)
        self.show('已安排 · 20 秒后提醒休息', seconds=3)
        self.b.tap(160, 192)
        self.panel = False
        self.report('主动提醒 · 等待 20 秒任务到期', {})
        self.wait_phase(3, 30)
        self.show('提醒已到达 · 开始休息 / 延后 / 暂停', seconds=3)
        self.b.tap(96, 144)
        self.wait_phase(4, 5)
        self.show('恢复计时 · 20 秒呼吸与伸展', seconds=21)
        self.b.tap(96, 176)
        self.wait_phase(5, 5)
        after = self.b.app()
        if after['completed_count'] != before['completed_count']+1:
            raise RuntimeError('完成记录未保存，请查看板子。')
        self.task_id = None
        self.b.tap(230, 225)
        self.panel = True
        self.show('恢复完成 · 用户反馈已保存', seconds=3)
        self.b.tap(160, 192)
        self.panel = False

    def run(self):
        self.report('正在连接板子…', {})
        initial = self.b.connect()
        app = self.b.app()
        if initial['workout'] or app['workout'] or any(1 <= t['phase'] <= 4 for t in app['tasks']):
            raise RuntimeError('板子有进行中的运动或提醒。请先完成，再启动展示。')
        self.initial_action = initial['action']
        self.prepared = True
        self.b.sync_clock()
        self.close_overlay()
        self.go(0)
        self.show('01 / 首页 · 星空表盘与猫咪陪伴', page=0)
        for action, title in enumerate(ACTION_NAMES):
            self.check_stop()
            self.select_action(action)
            self.show('动画 · '+title, action=action, overlay=0)
        self.select_action(self.initial_action)
        self.go(1)
        self.show('02 / 天气 · 信息卡片', page=1)
        self.b.down()
        self.show('天气 · 城市选择', overlay=6)
        self.b.tap(160, 62)
        self.show('天气 · 当前天气详情', overlay=6)
        self.b.swipe(235, 60, 85, 60)
        self.show('天气 · 天气预报', overlay=6)
        self.close_overlay()
        self.go(2)
        self.show('03 / 运动 · 运动入口', page=2)
        self.b.up()
        self.show('运动 · 准备开始', overlay=7, sport_page=0)
        self.owns_workout = True
        self.b.tap(160, 205)
        self.show('运动 · 步数', overlay=7, sport_page=1, workout=True)
        for page, title in [(2, '卡路里'), (3, '运动时长'), (4, '心率'), (5, '结束运动')]:
            self.check_stop()
            self.b.swipe(235, 50, 85, 50)
            self.show('运动 · '+title, sport_page=page, workout=True)
        self.finish_workout()
        self.go(3)
        self.show('04 / 健康 · 健康入口', page=3)
        self.b.up()
        self.show('健康 · 心率测量', overlay=4)
        self.b.tap(160, 120)
        self.show('健康 · 心跳动画与测量结果', seconds=4, overlay=4)
        self.b.swipe(235, 60, 85, 60)
        self.show('健康 · 心率历史', overlay=4)
        self.b.swipe(235, 60, 85, 60)
        self.show('健康 · 血压测量', overlay=4)
        self.b.tap(160, 120)
        self.wait(3)
        self.b.swipe(235, 60, 85, 60)
        self.show('健康 · 血压历史', overlay=4)
        self.b.up()
        self.expect(overlay=0)
        self.go(4)
        self.show('05 / 音乐 · 音乐入口', page=4)
        self.b.up()
        self.show('音乐 · 播放器', overlay=5)
        self.b.tap(226, 210)
        self.show('音乐 · 播放列表', overlay=5)
        self.b.up()
        self.b.tap(93, 210)
        self.show('音乐 · 音量面板', overlay=5)
        self.close_overlay()
        self.go(5)
        self.show('06 / 消息 · 通知卡片', page=5)
        self.reminder()
        self.go(0)
        self.show('本轮展示完成 · 返回首页', seconds=2, page=0, overlay=0, workout=False)

    def cleanup(self):
        if not self.prepared:
            return
        self.report('正在收尾并返回首页…', {})
        # Cleanup is bounded and only finishes actions started by this instance.
        old_stop = self.stop
        self.stop = threading.Event()
        try:
            if self.panel:
                self.b.tap(160, 192)
                self.panel = False
            if self.task_id:
                self.report('正在结束本次演示提醒（最多 30 秒）…', {})
                deadline = time.monotonic()+30
                while time.monotonic() < deadline:
                    task = self.task()
                    if not task or task['phase'] not in (1, 2, 3, 4):
                        break
                    if task['phase'] == 3:
                        self.b.tap(96, 144)
                    elif task['phase'] == 4:
                        self.b.tap(224, 176)
                    time.sleep(.5)
                task = self.task()
                if task and task['phase'] in (1, 2, 3, 4):
                    raise RuntimeError('本次提醒仍待处理，请在板子上完成或取消。')
                self.task_id = None
            if self.owns_workout:
                self.finish_workout()
            self.close_overlay()
            self.go(0)
            if self.b.state()['action'] != self.initial_action:
                self.select_action(self.initial_action)
            self.expect(page=0, overlay=0, workout=False)
        finally:
            self.stop = old_stop


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb')
    parser.add_argument('--adb-port', type=int, default=5038)
    parser.add_argument('--serial')
    parser.add_argument('--dwell', type=float, default=4)
    parser.add_argument('--autostart', action='store_true')
    parser.add_argument('--headless', action='store_true')
    parser.add_argument('--capture', help='Optional local screenshots for live verification')
    parser.add_argument('--log', type=Path)
    args = parser.parse_args()
    if not 1 <= args.adb_port <= 65535 or not .5 <= args.dwell <= 15:
        parser.error('invalid port or dwell')
    lock = socket.socket()
    try:
        lock.bind(('127.0.0.1', 47661))
    except OSError:
        if not args.headless:
            from tkinter import messagebox
            messagebox.showinfo('月薪喵自动展示', '展示程序已经打开，请使用现有窗口。')
        return 1
    logfile = args.log or Path(os.environ.get('LOCALAPPDATA', str(Path.home())))/'MoonCatDemo'/f'{dt.datetime.now():%Y%m%d-%H%M%S}.jsonl'
    logfile.parent.mkdir(parents=True, exist_ok=True)
    events = queue.Queue()
    stop = threading.Event()

    def report(title, state):
        with logfile.open('a', encoding='utf-8') as file:
            file.write(json.dumps({'time': dt.datetime.now().isoformat(timespec='seconds'),
                                   'title': title, 'state': state}, ensure_ascii=False)+'\n')
        if args.headless:
            print(title, json.dumps(state, ensure_ascii=False), flush=True)
        events.put(('status', title))

    def execute(dwell):
        demo = None
        error = None
        try:
            demo = Demo(Board(find_adb(args.adb), args.adb_port, args.serial), stop, report, dwell, args.capture)
            demo.run()
        except (Stopped, KeyboardInterrupt):
            stop.set()
        except Exception as exc:
            error = str(exc)
        finally:
            if demo:
                try:
                    demo.cleanup()
                except Exception as exc:
                    error = (error+'；' if error else '')+'收尾提示：'+str(exc)
        report(error or ('已停止，板子已回到首页。' if stop.is_set() else '展示完成，板子已回到首页。'), {})
        events.put(('done', error))
        return 1 if error else 0

    if args.headless:
        try:
            return execute(args.dwell)
        finally:
            lock.close()

    import tkinter as tk
    from tkinter import ttk
    window = tk.Tk()
    window.title('月薪喵 · 一键自动展示')
    window.geometry('610x480')
    window.resizable(False, False)
    window.configure(bg='#101b38')
    tk.Label(window, text='月薪喵  /  LIVE DEMO', bg='#101b38', fg='#bca9fa',
             font=('Microsoft YaHei UI', 12)).pack(anchor='w', padx=28, pady=(25, 8))
    tk.Label(window, text='把板子交给演示程序', bg='#101b38', fg='#fff9ef',
             font=('Microsoft YaHei UI', 23, 'bold')).pack(anchor='w', padx=28)
    tk.Label(window, text='六个主页面 · 六种动画 · 运动与健康 · 提醒反馈闭环',
             bg='#101b38', fg='#c4cde1', font=('Microsoft YaHei UI', 10)).pack(anchor='w', padx=28, pady=(12, 5))
    tk.Label(window, text='每轮约 5 分钟。演示期间请勿同时触摸板子。\n运动、健康和提醒演示会保存本轮记录。',
             justify='left', bg='#101b38', fg='#94a3c3', font=('Microsoft YaHei UI', 10)).pack(anchor='w', padx=28)
    status = tk.StringVar(value='连接好 USB，点击开始即可。')
    tk.Label(window, textvariable=status, bg='#1c2c51', fg='#fff9ef', wraplength=530,
             justify='left', anchor='w', padx=16, pady=14, height=3,
             font=('Microsoft YaHei UI', 11)).pack(fill='x', padx=28, pady=18)
    settings = tk.Frame(window, bg='#101b38')
    settings.pack(fill='x', padx=28)
    loop = tk.BooleanVar(value=False)
    tk.Checkbutton(settings, text='循环展示', variable=loop, bg='#101b38', fg='#e6def5',
                   selectcolor='#25375d', activebackground='#101b38', activeforeground='white',
                   font=('Microsoft YaHei UI', 10)).pack(side='left')
    tk.Label(settings, text='每页停留（秒）', bg='#101b38', fg='#c4cde1').pack(side='left', padx=(22, 5))
    dwell = tk.StringVar(value=str(args.dwell))
    ttk.Combobox(settings, textvariable=dwell, values=['2', '4', '6', '8'], width=4, state='readonly').pack(side='left')
    buttons = tk.Frame(window, bg='#101b38')
    buttons.pack(fill='x', padx=28, pady=18)
    active = [False]
    closing = [False]

    def start():
        if active[0]:
            return
        active[0] = True
        stop.clear()
        start_button.config(state='disabled')
        stop_button.config(state='normal')
        threading.Thread(target=execute, args=(float(dwell.get()),), daemon=True).start()

    def halt():
        stop.set()
        status.set('正在停止并收尾，请保持 USB 连接…')
        stop_button.config(state='disabled')

    def close():
        if active[0]:
            closing[0] = True
            halt()
        else:
            window.destroy()

    start_button = tk.Button(buttons, text='开始自动展示', command=start, bg='#b6a0ef', fg='#17213d',
                             relief='flat', padx=22, pady=8, font=('Microsoft YaHei UI', 11, 'bold'))
    start_button.pack(side='left')
    stop_button = tk.Button(buttons, text='停止并返回首页', command=halt, state='disabled', bg='#293959', fg='white',
                            relief='flat', padx=18, pady=8, font=('Microsoft YaHei UI', 11))
    stop_button.pack(side='left', padx=12)

    def poll():
        while not events.empty():
            kind, value = events.get()
            if kind == 'status':
                status.set(value)
            else:
                active[0] = False
                start_button.config(state='normal')
                stop_button.config(state='disabled')
                if closing[0]:
                    window.destroy()
                    return
                if not value and not stop.is_set() and loop.get():
                    window.after(2000, lambda: start() if loop.get() and not stop.is_set() and not closing[0] else None)
        window.after(100, poll)

    window.protocol('WM_DELETE_WINDOW', close)
    window.after(100, poll)
    if args.autostart:
        window.after(600, start)
    window.mainloop()
    lock.close()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
