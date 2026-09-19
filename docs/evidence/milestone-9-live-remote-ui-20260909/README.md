# candidate-06 实机接力记录（2026-09-09）

本轮依据 `../openvela-closure-20260908/NEXT-TASK-HANDOFF.md` 继续调试。用户已烧录并报告启动；本轮未写入镜像，未写 boot0，未提交或清理两个仓库的 dirty/untracked 文件。

## 已取得实证

- ADB 5037 单 daemon、serial `1234` 在线；读取编译时间 `Sep 9 2026 12:05:31`，`openvela_ui` PID 48、开机 `ai_agent` PID 49 存在。
- 从默认 `10.0.0.2` 恢复当前已保存的 WPA2 网络，DHCP 地址 `172.20.10.8`。凭据只在内存中处理，未输出。临时 SSH 密钥只推入 `/tmp`，SSH 强制核验已锁定主机身份。
- 截图 PASS：`01-home.png`，320×240，完整 PPM 长度校验后生成。请求 `956648841`，UI tick `496163`。
- 虚拟滑动 PASS：`02-swipe.png` 从主页进入天气页；请求 `956791921`，tick `639594`。下滑后 `03-control.png` 显示城市选择；请求 `956829867`，tick `677497`。
- 虚拟点击 PASS：点击 `(155,62)` 后 `04-tap.png` 显示北京天气详情；请求 `956850641`，tick `697969`。天气数据显示“网络连接失败/代理不可达”，天气联网功能未通过。
- 定时路由/建任务 PASS：`ws-trial.txt` 记录 UTF-8 请求“20秒后主动提醒我休息”，约 0.02 秒返回本地 `coach_schedule` 及一次性任务。
- 到期后列表为空：`list-first.txt`。随后立即请求得到 `noop/cooldown_active`，剩余 2570 秒：`cooldown-check/ws-trial.txt`。这证明冷却逻辑被激活，不独立证明提醒卡被显示。
- 到期后 UI 仍响应截图：`05-after-first.png`，请求 `956960542`，tick `807735`。此后冷却检查的 4 张采样也成功。

## 首轮采样失败与修复

`observation.log` 原样保留：WebSocket 子进程需要的 Python 3.11 `PYTHONPATH` 被截图子进程继承，遮蔽了 Python 3.12 的 Pillow，导致 `_imaging` 导入失败。首轮到期窗口漏采，提醒卡 **NOT_TESTED**，不能由任务删除/冷却推断屏幕 PASS。

`observe.py` 已将截图环境与 WebSocket 环境分离；每次发消息前必须完成一次真实截图预检，失败则不发送请求。`cooldown-check/` 已实机验证修复后预检及连续截图成功。一次仅有一个控制客户端串行采样。

## 独立重置与连接中断

为避免首轮触发的 45 分钟冷却影响新一轮，在明确报告“独立重复演示重置”后保存 Wi-Fi 配置并执行正常 `reboot`，没有 kill Agent。重启后设备进入 `VID_1F3A&PID_EFE8` FEL；当时 PhoenixSuit PID 20956 仍运行，随后已关闭。设备仍在 FEL，ADB 消失，需用户拔掉 USB 至少 10 秒再重插。本次不是连续无重置测试，不能记录为冷启动 PASS。

用户随后已重新插拔；ADB 恢复，uptime 不足一分钟，Wi-Fi 自动连接 `172.20.10.8`。重新引导 `/tmp` SSH 后完成下述第二轮。

## 第二轮完整闭环：PASS（单轮）

运行 `observe.py --name round2 --repeat`，保持这次启动的 Agent PID 48、UI PID 47，不重启进程，不修改冷却或时钟。

- `round2/ws-trial.txt`：0.03 秒返回定时任务；6.12 秒重复输入，6.14 秒返回“已存在，未重复创建”。
- `round2/observation.log`：预检及 32/32 序列截图成功。采样起点覆盖 0.00–84.55 秒，串行完整抓图实际间隔约 2.6–3.0 秒（包含 SSH 和传输），不是 1 Hz，也不是 FPS 测量。
- 已实际打开查看 `trial-07.png`（19.12 秒，无卡）、`trial-08.png`（21.78 秒，有卡）、`trial-09.png`（24.44 秒，有卡）、`trial-10.png`（27.05 秒，有卡）、`trial-11.png`（29.91 秒，无卡）、`trial-12.png`（32.75 秒，无卡）。这些是电脑发起抓图的相对时间，并非精确的板端弹出/消失时间。
- 三张卡片内容为 `MOONCAT ACTIVE COACH` / `[DEMO] Inactive for 75 minutes. Stretch for 60 seconds.`，显示期间猫姿态变化。卡片持续时间与源码 8000 ms 一致，但稀疏采样不单独测定精确持续时间。
- `round2/list-after.txt`：到期后 `No cron jobs scheduled.`。
- `round2/post-touch.log` / `post-swipe.png`：到期后滑动进入天气页成功，UI tick 221264/222320；SSH 继续响应，`device-before.txt` 与 `device-after.txt` 保持 UI PID 47 / Agent PID 48。
- 本轮 WS 观察共 96.36 秒，无连接断开记录。此轮屏幕渲染提醒闭环为 PASS；前一轮仍是漏采，不能计入完整视觉验证轮数。

## 证据边界

截图是活动 LVGL 屏幕渲染证据；虚拟触摸不属于实体手指证据。截图采样期间不测正常 FPS。本地路由是有限意图模型；主动休息输入为模拟观察，非真实久坐传感器结果。当前关闭截图、虚拟输入及一轮主动提醒卡闭环的实机缺口，后续重点是多样功能的真实链路覆盖、LLM/Skill、无网分流及交付；按用户明确调整，不以多轮重复计数作为验收门槛。正常重启进入 FEL 的操作风险仍需保留，后续不可无视 PhoenixSuit 残留。
