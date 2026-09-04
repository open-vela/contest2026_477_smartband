# Milestone 5：20 秒主动提醒演示

## 用户可见闭环

QuickApp 保留“立即提醒”，并新增橙色“20 秒后主动提醒”。主动路径要求端侧
Agent 依次执行 `get_current_time → cron_list → cron_add`，创建名为
`mooncat-demo-once` 的一次性 `at` 任务。到期后 cron action 调用既有
`mooncat_coach_tick(mode=execute, source=simulated)`，Tool 经 `mooncat` channel
把 `[DEMO]` 建议送给 native UI。

设置任务时不会提前调用 Tool。官方 cron 每 10 秒轮询，所以预期可见窗口为点击后
20–30 秒，不是硬实时。立即路径和主动路径共享进程内 cooldown；现场展示主动弹卡时，
应在新鲜 cooldown 状态下先点橙色按钮，两条路径不要连续执行。

## 最小实现范围

- `quickapp/mooncat-active-coach.js` 及其打包副本：生成一次性 cron 查询，固定
  `at` schema、字符串化 `action_args`、`system` 审计 channel 和 `mooncat` 动态通知；
- `quickapp/src/pages/index/index.ux`：同屏提供立即/主动两条路径，成功文案只要求用户
  核对 Agent 返回，不把自然语言回复误写成任务已创建；
- `quickapp/verify-proactive.mjs` 与 `tests/test_quickapp_demo.py`：验证 cron 模板可解析、
  execute 模式、模拟数据边界、重复任务检查和页面入口；
- 未改策略、Tool、native UI、cron 服务、固件配置或消息总线。

## 结论分层

| 层级 | 结果 | 证据边界 |
| --- | --- | --- |
| 实现 | PASS | 主动路径已进入 QuickApp 源码和页面 |
| 主机自动测试 | PASS | `20 passed, 1 skipped`；skip 为本机缺少 C 编译器 |
| QuickApp verifier | PASS | 原 verifier 与新增 proactive verifier 均通过 |
| 隔离 QuickApp 构建 | PASS | AIoT Toolkit 2.0.5 生成开发签名 RPK |
| 编译产物锚点 | PASS | 编译页面含 job 名、`cron_add` 和主动按钮文案 |
| QuickApp 实体按钮点击 | NOT_TESTED | 按上一里程碑边界放置，未继续追测 |
| 实机 cron 创建/到期触发 | NOT_TESTED | 未连接或操作 Gemini S1 |
| Tool → message bus → native UI 到期弹卡 | NOT_TESTED | 仅复用既有链路，无本里程碑实机证据 |
| 固件构建/镜像打包/启动/FPS | NOT_TESTED | 本里程碑只构建 QuickApp RPK |
| 烧录/FEL/boot0/分区写入 | NOT_PERFORMED | 没有新授权，也没有启动 PhoenixSuit |

RPK 是 toolkit 默认开发证书签名的调试产物，不是发布签名，也没有被侧载到设备。
完整本地结果见 `local-validation.txt`，Git 保护基线见 `baseline-safety.txt`，产物哈希见
`artifact.sha256`。

