# Milestone 6：离线中文工具路由器（烧录前）

## 目标闭环

中文请求先经过既有 prompt-injection guard 和关键词 fast paths，再进入 5 类 int8
TFLite Micro 模型。模型接受的 MoonCat 意图由确定性代码转换成既有 Tool JSON；低置信度
或非 MoonCat 请求返回 `NULL`，继续走原有联网 LLM。计划任务仍复用
`get_current_time → cron_list → cron_add → mooncat_coach_tick → mooncat channel`。

这个里程碑停在经过审计的候选镜像、真实烧录之前，不执行 PhoenixSuit、FEL、boot0、分区写入或
烧录。QuickApp/VAPP 与 `openvela_ui` 仍不能同时拥有 LCD；既有 QuickApp 实体按钮结果
继续保持 `NOT_TESTED`，不得用源码、RPK 或页面加载结果代替实体点击证据。

## 最小增量

- `local_router/`：训练、独立模板族测试集和模型产物；
- `agent/mooncat_local_router.*`、`mooncat_intent_model.*`：确定性工具计划与 TFLM 推理；
- `agent/packages_ai_agent_local_router.patch`：旧 MoonCat overlay 之后的第二 patch；
- `firmware/board/nsh_minidisplay-local-router.fragment`：TFLM 及本地路由 feature gate；
- `scripts/apply_local_router_overlay.sh`：只应用这个增量的幂等脚本；
- `scripts/build_gemini_s1_local_router.sh`：显式重配、构建和最终配置 gate。

未修改现有 QuickApp、MoonCat Tool、cron、native UI 或原 overlay 文件。

## 验收状态

| 层级 | 状态 | 当前证据边界 |
| --- | --- | --- |
| 第二 patch 严格应用/反向检查 | PASS | 在旧 MoonCat patch 已应用的隔离副本验证 |
| overlay `check/apply/apply/verify` 幂等 | PASS | 本地主机隔离目录，两次 apply 均通过 |
| 路由顺序契约 | PASS | guard 后调用；既有关键词路径先于本地模型；`NULL` 继续 LLM |
| 最终训练与独立测试指标 | PASS | int8 macro-F1 `0.991879`；冻结 acceptance exact match `0.987179`；全部目标通过 |
| 主机 TFLite 推理与 C/Python parity | PASS | QuickApp 长提示 `30/30`；float/int8 一致率 `0.998397`；六组逐索引 parity |
| hushen Gemini S1 完整构建 | PASS | build04 retry3 exit `0`；前三个真实失败及最小修复均保留 |
| `vela.bin` 模型/符号/尺寸审计 | PASS | 精确模型只出现一次；LTO-aware 函数证据；两算子；32 KiB 静态 arena |
| IMAGEWTY 候选与 22 项容器审计 | PASS | 16/16 gates；22 项边界、padding、对齐、回读、V-file、双基线差分均通过 |
| Gemini S1 模型延迟、arena 与峰值内存 | NOT_TESTED | 必须在获准烧录后实测 |
| QuickApp 实体按钮点击 | NOT_TESTED | 沿用上一里程碑边界，不继续追测 |
| Tool → `mooncat` → native UI 新固件显示 | NOT_TESTED | 不能由 host stub 或 ELF 字符串替代 |
| PhoenixSuit/FEL/boot0/分区写入/烧录 | NOT_PERFORMED | 本里程碑明确停止在真实烧录之前 |
| commit / push | NOT_PERFORMED | 未获授权 |

## 烧录前候选

- 路径：`/data/openvela-contest-2026-gemini-s1-v1-pack-20260830-04/output/Gemini-S1-MoonCat-AI-Local-Router-20260830-candidate-04.img`
- 大小：`71,915,520 bytes`
- SHA-256：`da8f82def499f297a5369c04dd4b6f040d70f340935b510883b4e5f1f9c92d6b`
- 相对 candidate03 和锁定 rollback 都只有 `nsh.fex`、`Vnsh.fex` 不同。

最终 ELF 使用 LTO，两个本地路由函数没有保留为普通最终符号；验收使用 LTO 输入对象的
`T` 定义、最终 ELF 的 DWARF 源定义、精确模型/arena 符号和 `vela.bin` 锚点组成证据链，
不得写成“最终 ELF 导出函数”。构建链接报告中的 32 KiB arena 是静态容量，不是板端峰值。

本地 patch 结果见 `integration-validation.txt`，最终模型结果见 `model-validation.txt`，
最终整仓回归与保护指纹见 `final-validation.txt`，
仓库保护快照见 `baseline-safety.txt`。build04 的成功构建、真实失败历史和 LTO 证据见
`remote-build-logs/`；pack04 的官方打包失败、私有 i386 runtime 成功路径、最终 16/16
审计和候选哈希见 `remote-pack-logs/`。
