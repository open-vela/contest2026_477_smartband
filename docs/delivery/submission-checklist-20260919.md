# 月薪喵完赛交付清单

核对日期：2026-09-19。正式专属仓：https://github.com/open-vela/contest2026_477_smartband ，目标分支 `dev-ai-contest-2026`。源码和材料通过 [PR #1](https://github.com/open-vela/contest2026_477_smartband/pull/1) 一并交付，最终合入时间和提交号以该 PR 的 GitHub 记录为准。

## 官方依据与提交方式

- [大赛总览](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/contest_overview.md)：9 月 20 日截止；GitHub 专属仓 fork → PR → 自行合入；另需独立介绍文件、≤5 分钟视频及专属仓地址。
- [代码提交指南](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/code_submission_guide.md)：大赛仅在 GitHub 进行；专属仓可自行合入，公共仓 PR 由维护者审核。
- [AI 硬件要求](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_hardware/ai_hardware_track_guide.md)：openvela + Agent 实机运行、LLM 对话、交互渠道、自定义 Skill 使用演示、至少一个主动执行场景及场景说明。

公开指南未给出另一份作品上传表或独立上传入口；本次把全部材料随源码提交至官方专属仓。GitHub 合入记录是本次交付凭据，不代表组委会已评审或确认完赛资格。

## 要求与证据

| 要求 | 本次交付 |
| --- | --- |
| 正式队伍专属仓与赛事分支 | 上述仓库与 PR #1；由 GitHub 记录核验合入 |
| openvela + ai_agent 真实硬件 | Gemini S1 candidate-08 已烧录、启动并实机验收；[milestone-11](../evidence/milestone-11-candidate08-live-20260919/) |
| LLM 后端与至少一个交互渠道 | DeepSeek 普通对话实测 0.907 秒；Agent WebSocket 请求与日志 |
| 自定义 Skill 及使用演示 | `skills/mooncat-active-coach.md`；板端 read_file → Tool trace，预览 4.422 秒 |
| 主动 + 执行场景 | 一次性任务创建、去重、到期弹卡、消失、任务清理及后续截图/滑动 |
| 用户故事、功能清单、技术说明 | [作品介绍 PDF](月薪喵作品介绍-20260919.pdf)、[DOCX](月薪喵作品介绍-20260919.docx) |
| 演示视频 ≤5 分钟 | [170.13 秒 MP4](月薪喵-candidate08-实机证据演示-20260919.mp4)，字幕 + 真实板端截图采样回放 |
| AI Coding 日志 | `logs/QinXi-ai/`：2 文件、1588 事件，官方校验 ALL OK |
| 至少一个有效开发 Skill | [单一 LVGL 显示所有权定位](../../developer-skills/gemini-lvgl-display-owner/SKILL.md) |
| Apache 2.0 | 仓库根目录 LICENSE；历史显示基线来源见 docs/58fps-source-closure.md |
| 公共仓修改单独 PR | [Agent #31 / UI #79 / LCD #45 / board #22](public-repo-changes.md)，待维护者审核 |
| 交付质量验证 | [milestone-12](../evidence/milestone-12-submission-validation-20260919/)：Linux 25 项、HTTP 32 项、Skill 25 项、四目标集成与重复应用 |

## 保留限制

恢复观察是明确标注的模拟输入；没有把它当成实体传感器证据。实体手指触摸、长期稳定性、最新 FPS/内存峰值、运动页与天气历史问题不计为已验收。视频是板端 LVGL 截图回放，不能证明实体 LCD 光学输出。完整源码工作区从零同步重编仍未验证；四个 overlay 基线已固定，其余依赖跟随赛事分支。公共 PR 提交不等于已获维护者接受。
