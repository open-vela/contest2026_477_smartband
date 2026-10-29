# 决赛开发：应用状态、任务和反馈

本分支增加真实应用事件流程；不把应用计时或用户确认当作传感器检测。
旧 `mooncat_coach_tick` 仍只接受 simulated，旧显式 QuickApp epoch 协议仍走演示 cron。

## 代码入口

- `common/mooncat_app_state.[ch]`：无网络、LVGL、板级依赖；调用方注入时钟、下一次本地午夜和保存函数。
- `agent/mooncat_app_service.[ch]`：NuttX flat build 下的一份共享状态，pthread mutex 串行访问，文本存储。
- `agent/tool_mooncat_app.c`：`mooncat_app_control` 的严格 JSON 校验与结果。
- `firmware/openvela_ui/openvela_ui_coach.c`：UI 线程轮询、COACH 面板、可操作卡片、恢复计时。
- `agent/packages_ai_agent_finals.patch` 与 `scripts/apply_overlay.sh`：源码 allowlist、Make/CMake 和 Tool 注册。新增补丁不更改旧补丁，可分别反向检查。

## 已接通的调用链

普通定时文本 → 五类模型中的 schedule → 确定性参数检查 → `mooncat_app_control(remind)` → 保存任务 → UI 每秒查询到期任务与最新上下文 → 创建卡片 → shown 确认 → 开始/延后/今天暂停 → 完成或取消。

普通模型列表请求查询新任务表；显式 `cron_list` 查询旧演示 cron。
准确匹配“开始专注”“暂停专注”“开启勿扰”“关闭勿扰”“恢复今天提醒”“查看恢复任务”走应用规则路径，不计作模型收益。
其他包含专注、勿扰或新应用 API 的表达不会被旧五类模型替换成模拟观察。
否定/条件执行请求、无效延迟、负数、小数和多数字时间不直接创建任务。
目前仅支持部分明确的否定/条件模式，不能当成完整自然语言语义保证。

UI 的 COACH 面板在各页面上方可进入同一状态；支持专注开关、45 分钟提醒、勿扰、恢复今天提醒和明确标为 Demo 的 20 秒演示。恢复界面确认按钮在计时结束前返回 too_early。运动模块 SPECIAL_STARTED/ENDED 事件进入同一状态；开始运动会中止尚未确认完成的恢复计时。

## 状态和接口约定

| phase | 意义 |
|---|---|
| 1 scheduled | 保存了身份、修订号、到期时间及恢复时长 |
| 2 queued | 到期策略允许进入待显示队列 |
| 3 shown | UI 线程已创建卡片并保存确认；不等于 LCD 扫描输出或实体触摸实测 |
| 4 recovering | 用户选择开始，应用恢复计时运行中 |
| 5 done | 用户在计时结束后确认完成，不是动作识别 |
| 6 cancelled | 用户取消/今天暂停，或运动打断恢复 |
| 7 expired | 未处理任务已超过到期时间一小时 |

`schedule(request_id, delay, duration)` 用稳定 request_id 去重；相同身份但参数不同返回 invalid。
`remind(delay, duration)` 为普通本地请求原子分配身份，相同参数的活动任务去重；完成/取消后再次请求会创建新任务。
`start/snooze/pause_today/complete/cancel` 携带实际 `id/revision`；只有上一条已接受的相同反馈重试返回 duplicate，其他旧操作返回 stale。`snooze` 还需要 delay。

任务表最多 8 条，同时作为有界待显示队列和最近记录。优先回收最老的终态记录；不覆盖活动任务，满时返回 queue_full。去重仅覆盖仍保留的记录。
到期时遇到勿扰、运动、今天暂停或另一项恢复，延后 300 秒重新检查。恢复今天设置不会抹掉已安排的延期时间。
每次卡片展示和反馈直接写入共享状态，不经过旧 bridge 的单 pending 字符串；旧模拟通知仍使用原 bridge。
新服务不调用 cron API，避免持有 cron mutex 时重入 cron。

## 保存、重启与时间

- 存储：`/data/etc/mooncat/app-state-v1.txt`；完整临时文件 → fflush/fsync → rename，再提交内存状态。
- 保存失败返回 storage_failed，逻辑状态不提交。文件格式/范围/重复身份校验失败时保留原记录，不静默重置。
- 这是带版本号的严格文本格式，未设计任意比特损坏检测或任意断电的恰好一次保证；文件系统的掉电语义仍需板端验证。
- 任务和偏好立即保存；纯计时进度每分钟检查点，崩溃可能丢失不足一分钟的专注累计。
- 重启停止专注/运动计时；未完成的近期显示/恢复任务延后 60 秒重新提醒，不自动累计完成；超过一小时的旧任务过期。
- 倒退时钟跳过本次到期处理；绝对到期时间保持不变。专注不累计倒退、超过 300 秒的跨步或关机时间。
- “今天”按设备 localtime 的下一次午夜计算，与现有 UI 日期口径一致。
- 调度由 Native UI 的 1 秒 timer 驱动。UI 不运行时不会显示新任务；这是当前实现边界，不是后台服务稳定性保证。

## 主机复现

使用 GCC/Clang、Python、pytest、numpy。完整适配测试需要 cJSON 1.7.19 的 `cJSON.c/.h`；通过 `MOONCAT_CJSON_DIR` 指向已有来源目录，不需要把第三方依赖复制进项目。
补丁组合测试通过 `MOONCAT_UPSTREAM_DIR` 指向 packages_ai_agent 固定基线 `31faed70f683a6f5e690437c5507891360f0814a` 的七个相关文件（具体列表在测试中）。

```sh
MOONCAT_CJSON_DIR=/path/to/cjson MOONCAT_UPSTREAM_DIR=/path/to/pinned-agent \
  python -m pytest -q -ra --basetemp=/tmp/mooncat-tests
```

测试编译实际 C 状态模块、存储服务、Tool 和 UI 回调。整链测试注入时间和模型分类，使用 LVGL API double 检查线程与操作顺序；它不渲染屏幕。独立路由评估运行真实主机 TFLite 模型和编译后的实际 C 分流代码，但 Tool 用记录桩替代，不执行网络或设备动作。

```sh
python scripts/prepare_finals_router_eval.py --upstream /path/to/pinned-agent --output-dir /path/to/router-eval
python scripts/evaluate_finals_router.py --binary /path/to/router-eval/route_eval --output /path/to/results.json
```

默认只运行 `finals_cases_v2.jsonl` 的 dev 分割。v1 保留旧 cron 期望和初次问题记录；v2 描述普通定时/查询迁移到应用任务表后的期望。不要把开发集修复结果称为独立验收。主机耗时字段只作主机诊断，不作为 Gemini S1 数字。

## 复用远端构建树时的链接检查

迁移过目录的增量构建树可能在 `apps/libapps.a` 留下旧绝对路径命名的对象。
即使日志显示新源码已经编译，链接器仍可能选择先出现的旧对象。本次在模型
性能日志缺失后，通过 `nuttx/nuttx.map` 确认选中了旧目录的模型对象。

复用既有树时，用工具链 `gcc-ar t` 查看归档成员，并核对 map 中模型、路由、
Tool 注册和新任务服务的实际来源。只对已确认存在当前替代对象的旧成员做
定向移除，原成员备份放在本机；随后重新链接并检查固件标记。不要只凭
源码相同或构建 exit=0 判断新实现已进入固件，也无需为此复制完整构建树。
该检查只证明构建接入，不替代设备执行测试。

## 仍需设备验证

模型适配器已增加 `[mooncat_model]` 结构化日志：初始化、特征及量化、Invoke、总耗时分开，另记录 TFLM `arena_used_bytes()` 与配置容量。Invoke 计时不包含日志输出；arena 字段不是系统堆峰值。使用 `scripts/collect_model_profile.py` 从实际设备日志抽取 CSV；没有样本时只输出表头并报告 NOT_MEASURED。

实体触摸、实际字体和布局、UI 文件 I/O 帧时延、掉电/重启、断开外网、真实运动页稳定性、长时运行，以及模型内核/arena/图形性能。当前没有新增传感器接入。
