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
   最新本地路由候选仍保持 pre-flash 状态，未声称沿用历史约 58 FPS 的实机结果。

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
python -X utf8 -m pytest -q
bash -n scripts/apply_overlay.sh
bash -n scripts/apply_local_router_overlay.sh
bash -n scripts/build_gemini_s1_firmware.sh
bash -n scripts/build_gemini_s1_local_router.sh
```

### 3. 应用 overlay 并构建 Gemini S1 固件

以下命令在参赛仓目录内执行，`..` 是 `repo sync` 生成的 openvela 工作区根目录：

```sh
./scripts/apply_overlay.sh --check ..
./scripts/apply_overlay.sh --apply ..
./scripts/apply_local_router_overlay.sh --check ..
./scripts/build_gemini_s1_local_router.sh .. 16
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
| Python/C 主机回归 | PASS | `22 passed, 1 skipped` |
| QuickApp 静态验证与隔离构建 | PASS | `docs/evidence/milestone-5-proactive-cron-demo-20260830-01/` |
| INT8 模型与主机路由行为 | PASS | `docs/evidence/milestone-6-local-router-20260830-01/model-validation.txt` |
| openvela Build04 与 `vela.bin` 静态门槛 | PASS | `docs/evidence/milestone-6-local-router-20260830-01/remote-build-logs/` |
| candidate-04 IMAGEWTY 静态审计 | PASS | `16/16`，见 `remote-pack-logs/` |
| 最新本地路由固件实机启动、延迟与 arena 峰值 | NOT_TESTED | pre-flash 边界 |
| 最新 Tool→Native UI、实体触摸与传感器输入 | NOT_TESTED | 未把模拟输入当实测 |
| PhoenixSuit/FEL/boot0/分区写入 | NOT_PERFORMED | 本次提交未执行硬件写入 |

## 七、AI Coding 使用说明

Codex 用于需求拆解、源码审计、最小功能实现、测试设计、远端构建失败定位、镜像静态
审计和证据文档整理。仓库保留真实失败与修复链，不只保留成功结论。可复用的端侧 Skill
见 `skills/mooncat-active-coach.md`；选定会话由组委会工具导出到 `logs/QinXi-ai/`，
自动脱敏后通过官方 `validate-log.py` 校验，未手工修改 JSONL 事件。

## 八、许可证

本作品按大赛要求采用 [Apache License 2.0](LICENSE)。
