# 月薪喵 Gemini S1 主动恢复教练

## 一、作品简介

月薪喵是一套运行在 Gemini S1 + openvela 上的腕上主动恢复教练。它把 Native
LVGL 产品界面、`ai_agent`、自定义 Skill/Tool、cron 主动任务和 QuickApp 入口串成
同一条可演示闭环：用户可以立即获取恢复建议，也可以创建一次性延时提醒；确定性快捷
路径未命中时，端侧 INT8 小模型先完成五分类工具路由，低置信度请求再回退到云端 LLM。

演示输入中仍有明确标记的模拟观测值。仓库不会把模拟输入、静态二进制检查或历史固件
结果描述成新固件的实机传感器、延迟、内存或触摸证据。

## 二、选题方向

本作品以“AI 硬件产品创新”为主，同时结合“手表应用创新”方向：

- openvela Native UI 提供腕上信息与主动提醒卡；
- `ai_agent` + MoonCat Skill/Tool 负责解释请求并执行动作；
- QuickApp 通过 `@system.velaclaw` 提供现场演示入口；
- TFLite Micro INT8 路由器在端侧识别立即执行、预览、定时、列表和 fallback 五类意图。

## 三、核心亮点

1. **主动任务闭环**：支持“20 秒后主动提醒”，按
   `get_current_time → cron_list → cron_add` 创建一次性任务，到期后再调用
   `mooncat_coach_tick`，而不是在创建任务时提前执行。
2. **端侧小模型路由**：发布模型为 52,008 bytes；冻结验收记录为 INT8 macro-F1
   `0.991879`、长 QuickApp 协议提示 `30/30`。模型只接管既有快捷路径未命中的请求。
3. **可审计固件构建**：overlay、板级配置、构建和 IMAGEWTY 打包证据均保留输入、
   失败历史与最终门槛，避免把“能编译”混同于“已上板运行”。
4. **显示基线继承**：Native UI 源码继承已实机测量的 sparse refresh 与 stride 修复；
   candidate-08 已实机验证远程截图、虚拟滑动、DeepSeek 对话、自定义 Skill 使用及定时提醒卡闭环；未沿用历史约 58 FPS 作为本次测量结果。

## 四、目录结构

- `firmware/openvela_ui/`：Gemini S1 Native LVGL 产品 UI；
- `firmware/lvgl/`、`firmware/board/`：LCD stride 修复、板级配置和启动脚本；
- `agent/`、`common/`：MoonCat Tool、主动策略与本地模型接入；
- `skills/`：部署到端侧的 MoonCat 主动恢复教练 Skill；
- `local_router/`：训练脚本、冻结数据结果与 INT8 模型；
- `quickapp/`：通过 `@system.velaclaw` 联动端侧 Agent 的 QuickApp；
- `scripts/`：受控 overlay、构建和配置合并脚本；
- `tests/`：主机行为、构建契约和本地路由测试；
- `docs/evidence/`：各里程碑的构建、打包与证据边界；
- `logs/`：经组委会采集器导出并脱敏的 AI Coding 日志；
- `contest2026_477_smartband.xml`：组委会 `repo` manifest 入口。

## 五、拉取、核验与构建

### 1. 拉取官方工作区

```sh
repo init -u https://github.com/open-vela/contest2026_477_smartband \
  -b dev-ai-contest-2026 -m contest2026_477_smartband.xml
repo sync -c -j8
cd contest2026_477_smartband
```

### 2. 仓内测试

```sh
# Linux: Python 3.10+、C 编译器、Node.js 18+
python -m pip install pytest numpy
python -X utf8 -m pytest -q
bash -n scripts/apply_overlay.sh
bash -n scripts/apply_local_router_overlay.sh
bash -n scripts/build_gemini_s1_firmware.sh
bash -n scripts/build_gemini_s1_local_router.sh
```

### 3. 应用 overlay 并构建 Gemini S1 固件

以下命令在参赛仓目录内执行，`..` 是 `repo sync` 生成的 openvela 工作区根目录：

```sh
bash scripts/apply_overlay.sh --check ..
bash scripts/apply_overlay.sh --apply ..
bash scripts/apply_local_router_overlay.sh --check ..
bash scripts/build_gemini_s1_local_router.sh .. 16
```

`--check` 只检查目标布局和 patch 可应用性；`--apply` 只覆盖 allowlist 文件并合并
赛事配置；`--verify` 可逐项复核应用结果。脚本不会执行 `git clean`、删除其他源码、
打包镜像或烧录。构建脚本显式选择 Gemini S1 `nsh_minidisplay` 配置并检查最终
TFLite Micro、QuickApp/VAPP、`ai_agent` 和 `velaclaw` 开关。

QuickApp 的验证与构建方式见 `quickapp/README.md`。完整源码闭包和证据口径见
`docs/58fps-source-closure.md`、`docs/contest-config.md` 与 `docs/verification.md`。

## 六、验证状态

| 项目 | 状态 | 证据 |
| --- | --- | --- |
| Linux 主机回归（2026-09-19） | 25 PASS / 0 SKIP | [本日验证记录](docs/evidence/milestone-12-submission-validation-20260919/)；HTTP 32 项与 Skill 25 项另行通过 |
| 四个公共仓目标的全新 overlay 集成 | PASS | 同上；check、apply、重复 apply、verify；不等于完整固件重编 |
| QuickApp 静态验证与隔离构建 | PASS | `docs/evidence/milestone-5-proactive-cron-demo-20260830-01/` |
| INT8 模型与主机路由行为 | PASS | `docs/evidence/milestone-6-local-router-20260830-01/model-validation.txt` |
| openvela Build04 与 `vela.bin` 静态门槛 | PASS | `docs/evidence/milestone-6-local-router-20260830-01/remote-build-logs/` |
| candidate-04 IMAGEWTY 静态审计 | PASS | `16/16`，见 `remote-pack-logs/` |
| candidate-06 启动、截图、虚拟触摸及定时 Tool→Native UI | PASS | `docs/evidence/milestone-9-live-remote-ui-20260909/` |
| 最新本地模型内核延迟与 arena 峰值 | NOT_TESTED | 响应耗时不等于模型内核测量 |
| 实体手指触摸与真实传感器输入 | NOT_TESTED | 虚拟输入和模拟观察不作为实体证据 |
| candidate-08 DeepSeek / 自定义 Skill / 定时提醒 | PASS | `docs/evidence/milestone-11-candidate08-live-20260919/`；0.907 秒短对话、4.422 秒 Skill 预览，均为单次实测 |
| candidate-08 连接稳定性 | 有限制 | 初次 ADB offline，冷插后经 SSH 完成验收；不作为长期稳定性结论 |
| 运动页 / 天气服务 / 本轮断网 | 未关闭 | 历史运动失联与天气联网失败保留；本轮未验收 |
| candidate-08 烧录 | 用户已执行 | 用户报告烧录，系统启动及上述功能验收通过；未读回全盘，未写 boot0 |

## 七、AI Coding 使用说明

Codex 用于需求拆解、源码审计、最小功能实现、测试设计、远端构建失败定位、镜像静态
审计和证据文档整理。仓库保留真实失败与修复链，不只保留成功结论。可复用的端侧 Skill
见 `skills/mooncat-active-coach.md`；选定会话通过官方采集器及原生 Codex 适配流程导出到 `logs/QinXi-ai/`，
自动脱敏后通过官方 `validate-log.py` 校验：2 个文件、1588 条事件，未手工修改 JSONL 事件。详见 [日志说明](logs/README.md)。

## 八、交付材料与演示入口

- 9 月 19 日实机验收版作品介绍：[PDF](docs/delivery/月薪喵作品介绍-20260919.pdf)、[DOCX](docs/delivery/月薪喵作品介绍-20260919.docx)。
- [2 分 50 秒实机证据演示视频](docs/delivery/月薪喵-candidate08-实机证据演示-20260919.mp4)：字幕版，使用本轮板端 LVGL 截图原时间间隔采样回放与真实请求/工具日志，非摄像机实拍。
- [现场操作与 4 分 30 秒录制脚本](docs/delivery/demo-runbook.md)：Native UI 独占屏幕，先演示定时任务，随后展示预览和列表。
- [公共仓改动与复现状态](docs/delivery/public-repo-changes.md)：配套 PR 与干净构建是独立交付门槛。
- [可复用显示故障定位开发 Skill](developer-skills/gemini-lvgl-display-owner/SKILL.md)，与设备运行时 Skill 分开提供。
- [板端截屏与虚拟触摸](docs/delivery/remote-ui-inspection.md)：使用现有 SSH；candidate-06 已验证截图/虚拟点击和滑动，candidate-08 已复验截图和滑动。

本次交付按官方 GitHub 流程将源码、独立介绍文档、演示视频和开发日志一并纳入专属仓 PR #1，合入状态以 [PR #1](https://github.com/open-vela/contest2026_477_smartband/pull/1) 为准。详见 [逐项交付清单](docs/delivery/submission-checklist-20260919.md)。candidate-08 同次启动核心链路已通过；完整官方源码工作区从零同步重编仍为 NOT_TESTED，已完成的四目标集成验证不能替代该项。

## 九、许可证

本作品按大赛要求采用 [Apache License 2.0](LICENSE)。
