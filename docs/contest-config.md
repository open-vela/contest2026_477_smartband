# Gemini S1 赛事配置差异

## 原则

`firmware/board/nsh_minidisplay.defconfig` 是哈希锁定的约 58 FPS 产品配置，不直接
编辑。AI 赛事能力只记录在 `nsh_minidisplay-ai-contest.fragment`，由
`scripts/merge_defconfig.py` 幂等合并。这样可以独立审计性能基线与 AI 增量。

## 当前增量

- 显式重放 r8 运行配置中的 `LV_USE_ANIMIMG`、`LVX_USE_DEMO_OPENVELA_UI`、
  `/data/openvela_ui` 与 HTTP 对时；这些设置存在于已测 r8 `.config`，但当时未回写
  到锁定 defconfig，若不补齐，fresh configure 会静默丢失产品 UI；
- 启用端侧 `ai_agent`；
- 把持久化根目录显式设为 `/data/agent`，使 Skill 的实际运行时路径与赛事文档的
  `/data/agent/skills/` 一致；
- 启用 `@system.velaclaw`，供 QuickApp 发起自然语言配置和查询；
- 启用 ai_agent 实际联网所需的 mbedTLS socket 适配，以及网络管理调用 NSH 所需的
  `popen()/pclose()`；
- 将 POSIX mqueue payload 上限由 1024 提高到 4096；
- 使用 shell allowlist；
- 显式关闭独立 Feishu、Weixin、MQTT、Node、MCP 和 agent 自带 LVGL 聊天 UI，
  避免与产品 UI 重复并控制内存/FPS 风险。

产品基线已有 `FEATURE_FRAMEWORK`、`QUICKAPP`、网络、mbedTLS 和 `UORB`。官方
Gemini S1 的 `dev-ai-contest-2026` defconfig 本身仍未启用 ai_agent，MQ 上限仍是
1024，因此不能整份复制官方 defconfig 来替换已验证产品配置。

## 证据边界

当前 58 FPS 配置没有 `FITNESS_ALGO` 和 `VIBRATOR`。首里程碑可以证明定时主动
触发、自定义 Tool 执行、message bus 推送和 native UI 响应，但输入状态必须明确
标注为 `simulated` 或 `simulated_or_derived`，不得写成真实传感器或震动闭环。

QuickApp 的 `velaclaw.ask()` 可可靠返回 `reply`，但现有 bridge 没有 unsolicited
主动推送能力；因此 QuickApp 只承担配置/查询入口，主动提醒由 native UI 的
message-bus tap 展示。
