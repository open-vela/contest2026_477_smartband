# 月薪喵主动恢复教练 QuickApp 演示

这是一个可直接展示的 openVela QuickApp 入口。点击“立即提醒”后，应用
通过 `@system.velaclaw.ask({ query })` 要求“月薪喵主动恢复教练”Skill 调用
`mooncat_coach_tick(mode=execute, source=simulated)`：QuickApp 显示
`res.reply`，策略命中且不在 cooldown 时，Tool 同时向 `mooncat` channel 推送
提醒，由 native UI 展示。

点击“20 秒后主动提醒”时，Agent 会先读取当前时间和检查同名任务，再创建一次性
`at` cron。到期后由 cron action 调用同一个 Tool，native UI 自动出现提醒卡；
QuickApp 不会在设置任务时提前调用 Tool。官方 cron 每 10 秒轮询一次，因此现场
可见窗口预计为点击后的 20–30 秒，不应描述为硬实时触发。
两条演示路径共享 Tool 的进程内 cooldown；若要展示主动到期弹卡，应在尚未执行
“立即提醒”的新鲜 cooldown 状态下先点橙色主动按钮，而不是连续点击两条路径。

## 展示链路

1. QuickApp 发送固定的 `[DEMO]` 模拟观察值；
2. “立即提醒”让 Skill 直接调用 `mooncat_coach_tick(mode=execute)`；
3. “20 秒后主动提醒”让 Agent 执行 `get_current_time → cron_list → cron_add`；
4. cron 到期后调用 Tool，Tool 命中时推送 native UI；
5. Agent 的执行结果显示在 QuickApp 页面。

helper 的默认模式仍是 `preview`，便于安全测试；现场按钮显式选择 `execute`。

## 核验与构建

```sh
npm run verify
npm install
npm run build
```

构建工具会从 `src/manifest.json`、`src/app.ux` 和
`src/pages/index/index.ux` 生成调试包。实际调用还要求目标 openVela 镜像已注册
`system.velaclaw`，并且端侧 `ai_agent` 已启动和配置。
