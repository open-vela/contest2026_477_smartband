# candidate-04 实机验收记录

## 候选与写入

- 候选：`Gemini-S1-MoonCat-AI-Local-Router-20260830-candidate-04.img`，本机短路径 `E:\GeminiFlash\closure-20260908\candidate04.img`。
- 71,915,520 bytes；烧录前完整 SHA-256 核对为 `DA8F82DEF499F297A5369C04DD4B6F040D70F340935B510883B4E5F1F9C92D6B`。
- 锁定回退镜像身份已核对且未修改。板端 `/data` 的 332 个文件、52,072,673 bytes 已备份到仓外私有目录，逐文件长度检查通过；不公开配置或凭据。
- 用户明确授权全盘擦除升级；最终由用户点击，并报告烧录完成。PhoenixSuit 预先显示 candidate04 路径、全盘擦除已选中、设备连接成功。

## 首次启动读取

用户报告写入完成后，本次通过 ADB 5037 / serial `1234` 实际读到：

```text
NuttX  0.0.0  Aug 30 2026 04:40:23 arm nsh
PID 48 openvela_ui
PID 49 ai_agent
```

与烧录前 8 月 29 日编译时间不同。首次系统启动与两个进程存在为 PASS；不能由此推断屏幕、触摸或任务闭环通过。

## 冷启动复核

退出 PhoenixSuit 后，ADB 消失；USB 枚举为 `VID_1F3A&PID_EFE8`（FEL）。用户断开 USB 至少 10 秒再重插后，重新读到同一固件版本，并由用户现场确认屏幕正常。Wi-Fi/SSH 也已恢复。

## 主动执行阻断

首轮经 UTF-8 SSH CLI 发送“20秒后主动提醒我休息”，返回 `[LOCAL AI 100% coach_schedule]`；重复请求返回已存在、未重复创建。到期附近用户报告屏幕卡住，SSH 超时且 ADB 命令无响应。首轮曾重启 Agent 以接入 CLI，存在测试干扰。

冷启动后改用固件自带 WebSocket（28789）保持原始 Agent 进程。请求约 0.02 秒返回任务创建，6 秒后重复请求约 0.02 秒返回去重；到期附近再次出现用户确认的屏幕卡死、SSH 超时。故此失败不能仅由首轮 CLI 重启解释。该时间是端到端响应时间，不是模型推理内核延迟。

源码确认 Agent outbound 回调调用 `lv_async_call()`，后者内部调用 `lv_malloc` 和 `lv_timer_create`；历史 Build04 实际配置为 `CONFIG_LV_USE_OS=0`。已准备最小修复：Agent 回调只更新互斥保护的待显示消息，由 Native UI 创建的 100 ms LVGL 定时器取出显示。修复是否解决卡死仍待新镜像实机验证，当前不得关闭主动闭环门槛。

| 项目 | 状态 |
| --- | --- |
| 首次启动、固件编译时间读取 | PASS |
| 冷启动与初始屏幕 | PASS：命令读取及用户现场确认 |
| 实体触摸 | NOT_TESTED |
| 本地定时意图与任务创建 | PASS |
| 重复任务去重 | PASS |
| 到期执行后持续运行/提醒卡 | FAIL：两次卡死，第二次保持开机 Agent |
| LLM、Skill、三轮定时主动执行、无网分流 | 未完成 |
| 板端模型延迟、arena、峰值内存 | NOT_TESTED |

全盘擦除后 Wi-Fi 回到固件默认配置；需要重新恢复已授权网络。SSH 主机身份仍须核验，不公开 SSID、密码和私钥。
