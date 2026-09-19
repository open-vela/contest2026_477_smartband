# candidate-08 核心链路实机验收

日期：2026-09-19。用户烧录 candidate-08 后，在 Gemini S1 上完成本轮验收。核心链路 PASS；不表示全部产品页面或最终赛事交付已完成。

## 固件与运行边界

烧录前本机候选为 `E:/GeminiFlash/closure-20260909/candidate08.img`，71,915,520 bytes，SHA-256 `42C510E49CF1C43D1805D050D2026DEC2E1D0815EC1A7899D41E4203EB86B54D`，与 milestone-10 一致。用户报告烧录完成；板端编译时间为 `Sep 9 2026 14:28:20`。未做板端 flash 全量读回，编译时间本身不是独立镜像身份校验。

初次启动能读取 UI/Agent 进程，随后 ADB offline。用户冷插后恢复 WPA2 热点和严格核验主机身份的 SSH；全盘擦除后的 Agent 配置从私有备份恢复，正常重启后 Wi-Fi 自动重连。以下云端、Skill、定时及操作验收均在这次重启后完成，未中途重启 UI 或 Agent。

## 实际结果

| 验收 | 结果和证据 |
|---|---|
| 启动及截图 | PASS。`01-boot.png` 为 320×240 板端 LVGL 主页 |
| 普通云端对话 | PASS。`cloud-01.jsonl`：0.907 秒收到 `MOONCAT_READY`；日志确认 DeepSeek 后端和真实 LLM 响应 |
| 读取并使用自定义 Skill | PASS。`skill-01.jsonl`：4.422 秒得到 preview 结果。`tool-evidence-01.txt` 记录同一 trace 的 `read_file`、读取 873 bytes、`mooncat_coach_tick` 的真实调用及参数 |
| 自定义输入保持 | PASS。传入 inactivity=10、sleep_debt=150、stress=20、battery=80，实际工具参数相同；返回 `sleep_debt_120m`、300 秒休息、delivered=false，没有被旧 battery 关键词分流 |
| 定时及去重 | PASS。`reminder01/ws-trial.txt`：0.02 秒建任务，6.11 秒重复输入，6.12 秒收到未重复创建 |
| 到期执行与提醒卡 | PASS。日志记录到期调用、delivered 和一次性任务删除；已实际查看 `trial-09.png` 的 `[DEMO] Inactive for 75 minutes. Stretch for 60 seconds.` 卡片，以及 `trial-12.png` 卡片消失后的界面 |
| 连续截图 | PASS。预检及 20/20 序列截图成功；采样约每 2.6 秒一次，包括 SSH 和传输，不属于动画 FPS 测量 |
| 后续操作 | PASS。`02-post-reminder-swipe.png` 显示滑动后的天气入口；UI/Agent 持续运行，SSH 响应 |
| 列表清理 | PASS。`list-after.jsonl` 返回无定时任务 |
| 冷却 | PASS。到期后的新定时仍可创建，但执行时返回 noop 并删除；`immediate-after.jsonl` 显示 `cooldown_active`、remaining=2590、delivered=false。不要描述为“冷却期拒绝所有建任务请求” |

`device-after.txt` 的 `free` 是一次系统堆观测：used=6,866,476 bytes，maxused=8,564,568 bytes。它不是路由模型独占内存，也不是完整压力测试的内存峰值。

## 尚未关闭

- 断网行为和路由模型独立延迟/内存尚未做本轮专门测量。
- 运动入口历史存在整机失联，天气详情历史联网失败；本轮未进入运动页，未验收天气服务。
- 虚拟输入和 LVGL 截图不等于物理触摸、摄像机拍摄的 LCD 或真实传感器证据。所有恢复观察为明确标注的模拟值。
- 短期核心链路通过不代表长期稳定性；首次 ADB offline 保留为连接问题。
- 正式仓合入、干净复现、更新后的独立材料、演示视频及赛事提交回执仍需分别关闭。
