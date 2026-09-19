# 月薪喵：主动恢复提醒助手

面向久坐学习和办公场景的 openvela 开发板原型。

## 从“知道该休息”到“到点提醒”

学习或工作投入时，人容易忽略休息时间。月薪喵让用户用一句话安排一次恢复提醒，由设备管理任务，到点执行并显示猫咪提醒卡。作品的价值是把请求、任务和执行连接起来，让提醒真正发生。

一个用户故事：用户准备继续学习，提出“20 秒后主动提醒”。Agent 建立一次性任务；到期后调用 MoonCat Tool，将恢复建议送到 Native UI。正式演示使用明确标注的模拟久坐观测，20 秒是演示等待时间，不是健康建议。

## 功能与作品范围

核心功能包括恢复建议、预览、一次性定时提醒、任务列表和低置信度云端回退。图形界面由 Gemini S1 上的 openvela Native LVGL 提供。当前已验证主线由 Native UI 独占屏幕，通过 Agent WebSocket 渠道交互；电脑可经 SSH 获取板端截图并发送虚拟点击和滑动。QuickApp 保留为独立入口，本轮不承诺与 Native UI 同时拥有 LCD。

作品定位为开发板原型，不宣称量产手环。真实传感器、新语音链路与跨设备控制不属于本次冻结范围。

## 系统如何完成主动执行

请求进入 Agent 后，确定性快捷路径优先处理覆盖意图。本地 INT8 模型对未命中的请求做工具路由；无法可靠覆盖时交给云端 LLM。定时请求复用 `get_current_time → cron_list → cron_add`，到期执行 `mooncat_coach_tick`，经 MoonCat 通道显示原生提醒卡。

端侧运行时 Skill 描述主动恢复场景，Tool 执行策略并产生结果，cron 负责调度。模型只做有限意图的工具路由，不是完整离线大语言模型。重复任务与提醒冷却影响实际行为，现场演示按真实状态展示。

## 工程亮点

五分类 INT8 路由器的发布模型为 52,008 bytes。冻结离线测试的 macro-F1 为 0.991879，QuickApp 长协议提示验收为 30/30；这些结果来自历史模型验收，不是 9 月 8 日重新训练或板端性能测量。

显示代码继承 sparse refresh 和实际 stride 修复，减少无变化像素刷新。历史固件有约 58 FPS 的设备采样；最新候选需另测，不能直接继承这一性能数字。Native UI 与 QuickApp 的 LCD 所有权是演示安排的明确约束。

## 当前证据（2026-09-19）

candidate-08 已由用户烧录到 Gemini S1。恢复配置并正常重启后，在同次启动中完成普通 DeepSeek 对话、自定义 Skill 读取和工具执行、一次性定时、重复请求去重、到期提醒卡、自动消失、任务删除及后续虚拟滑动。短对话端到端耗时 0.907 秒；Skill 预览请求为 4.422 秒，两者为本轮单次实测，不能代表所有请求的延迟。

Skill 的实机 trace 记录了读取 mooncat-active-coach.md 和调用 mooncat_coach_tick，保留了输入的模拟睡眠债、压力、电量等参数，预览返回 delivered=false。定时请求在 0.02 秒建立任务，重复请求未多建任务；预检及 20 张序列截图全部成功，其中捕获提醒卡出现和消失。冷却期立即提醒返回 cooldown_active；新定时可建立，但到期执行为 noop。证据见 milestone-11-candidate08-live-20260919。

本日 Linux 主机回归为 25 passed、0 skipped；另以实际 C 源码验证 HTTP chunked 接收 32 项、Skill 路由 25 项。四个公共仓目标的全新 overlay 应用和重复应用均通过。首次启动后出现过 ADB offline，冷插并切换 SSH 后完成上述验收。运动入口历史失联、天气服务、无网行为及模型独占资源指标尚未关闭。截图证明板端 LVGL 渲染结果，不作为物理触摸或真实传感器证据。

## AI 开发过程

AI Coding 用于需求拆解、功能实现、主机回归、构建失败定位和证据整理。仓内保留选定且脱敏的官方格式日志。设备运行时 Skill 位于 `skills/mooncat-active-coach.md`；可复用开发 Skill 位于 `developer-skills/gemini-lvgl-display-owner/SKILL.md`，讲清显示所有权故障的输入、诊断步骤和验收标准。

## 复现与交付

正式仓：https://github.com/open-vela/contest2026_477_smartband

目标分支：`dev-ai-contest-2026`。源码、实机证据和材料通过正式仓 PR #1 一并交付，合入状态以 GitHub 记录为准。拉取与构建命令见 README；四个公共仓配套 PR 见 `docs/delivery/public-repo-changes.md`，等待维护者审核。本版配套 2 分 50 秒字幕演示视频，使用真实板端截图采样和请求/工具日志。完整工作区从零同步重编仍未验证，四目标 overlay 集成验证不等于固件重编。

作品采用 Apache License 2.0。最终提交以同一代码版本、实机证据、独立介绍文件和不超过 5 分钟的演示视频为一组。
