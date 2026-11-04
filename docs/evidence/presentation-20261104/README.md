# 2026-11-04 实机截图

设备：Gemini S1，openvela Native LVGL，固件 `NuttX Nov 3 2026 03:51:56`。通过 `scripts/mooncat_control.py` 的 ADB 截图与虚拟触摸采集，原始画面为 320×240。

设备启动后日历为 1970 年，采集前已同步到 2026 年 11 月 4 日的本地时间。保持原 Focus off / Quiet off 设置，执行 Try 20s → Take a break → Feeling better，读取应用状态确认完成记录保存，结束后返回首页。

| 文件 | 内容 |
| --- | --- |
| `home-20261104.png` | 当日首页与日期 |
| `rest-panel-20261104.png` | Rest 面板 |
| `scheduled-20261104.png` | 20 秒任务已安排 |
| `reminder-20261104.png` | 到期提醒卡 |
| `recovery-20261104.png` | 恢复倒计时 |
| `recovery-ready-20261104.png` | 倒计时完成，等待用户确认 |
| `completed-20261104.png` | 完成后返回面板 |
| `sport-entrance-20261104.png` | 运动入口 |
| `health-20261104.png` | 健康入口 |

当日结束 UI 状态：`page=0, overlay=0, action=0, workout=false, health_measuring=false`。运动与健康的完整流程验证见 [11 月 3 日记录](../architecture-20261102/g2d-final/)。
