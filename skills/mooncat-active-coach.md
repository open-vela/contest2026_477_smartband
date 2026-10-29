# 月薪喵主动恢复教练

决赛开发版增加 `mooncat_app_control`，来源为 `app_event`，表示用户操作和应用计时；不是传感器观测。新应用流程优先使用本接口。下文原 `mooncat_coach_tick` / cron 流程保留为明确标注的模拟演示，两种任务列表分开。

## 应用计时与反馈（决赛开发版）

- 开始/暂停专注：`{"op":"focus_on"}` / `{"op":"focus_off"}`；这只改变专注计时，安排提醒需要另一个 schedule 操作。
- 创建提醒：`{"op":"schedule","request_id":1001,"delay":2700,"duration":60}`，秒为单位；同一请求重试必须保留 request_id、delay、duration。不同请求使用不同的正整数 request_id。先 list 查看已有任务，避免身份冲突；只在最近保留的 8 条记录内去重。
- 普通本地定时请求和 UI 使用 `{"op":"remind","delay":2700,"duration":60}`；服务原子分配身份，相同延时和恢复时长的活动任务去重。任务已完成或取消后再次提出提醒请求会建立新任务，需要跨完成重试时使用上面的 schedule/request_id。
- 查阅：`{"op":"list"}`；任务 phase：1 已安排、2 已排队、3 UI 已构建卡片、4 恢复计时、5 用户确认完成、6 已取消、7 已过期。queued 不等于 shown，shown 也不是 LCD 实测。
- 用户设置：`dnd_on`、`dnd_off`、`workout_on`、`workout_off`、`resume_today`。运动表示用户开启的应用模式，不是检测到了运动。
- 反馈：`{"op":"start","id":1,"revision":3}`、`{"op":"snooze","id":1,"revision":3,"delay":300}`、`pause_today`、`complete`、`cancel`。每次先读取实际 id/revision；不得照抄示例数字。complete 只能在计时结束后由用户确认。
- `storage_failed`、`stale`、`busy`、`queue_full` 都不表示执行成功；读回最新状态再解释，不能自动把旧卡片反馈套到新修订号上执行。
- Native UI 的 COACH 面板可本地设置、创建 45 分钟提醒或明确标为 Demo 的 20 秒演示，并操作提醒。新任务由 UI 线程每秒检查；UI 未运行时不会出卡，重启后恢复任务状态。勿扰/运动/今天暂停会让到期任务延后 5 分钟再检查。
- 到期只读当前应用状态，绝不把专注秒数转换为“实测久坐”。损坏的存储保留并返回错误，不能静默清空。

## 原有模拟演示流程

使用确定性端侧策略评估来源明确的手环状态，并通过持久化 cron 任务主动执行恢复提醒。首个里程碑只接受明确标注为 simulated 的演示数据，不能描述为真实传感器测量。

## When to use

当用户要求查询恢复建议、开启主动恢复教练、设置久坐提醒，或检查教练任务时使用。

## How to use

1. 先调用 `get_current_time` 获取当前时间；创建一次性任务时必须据此计算 `at_epoch`。
2. 明确确认数据来源。当前里程碑只能把 `source=simulated` 传给 `mooncat_coach_tick`；不得把 UI 生成的心率、压力、睡眠或活动值写成实机数据。
3. 只查询建议时调用 `mooncat_coach_tick`，设置 `mode=preview`，并完整传入 `valid`、`workout_active`、`do_not_disturb`、`inactivity_minutes`、`sleep_debt_minutes`、`stress_score` 和 `battery_percent`。
4. 需要立即演示执行时，把同一观察传给 `mooncat_coach_tick`，设置 `mode=execute`。只有策略命中且不在 cooldown 时，工具才会向 `mooncat` channel 推送带 `[DEMO]` 的通知。
5. 需要主动任务时，先调用 `cron_list` 去重，再调用 `cron_add`。`schedule_type` 只能是 `every` 或 `at`；分别使用 `interval_s` 或 `at_epoch`。设置 `channel=system` 作为 cron 框架审计回执，并设置 `action=mooncat_coach_tick`；动态建议由 Tool 自己推送到 `mooncat` channel，避免 UI 双重弹窗。`action_args` 必须是字符串化 JSON，不能传 JSON 对象。
6. 解释工具返回的 `status`：`delivered` 表示已执行消息总线推送，`preview` 表示只返回建议，`noop` 表示无建议或仍在 cooldown。始终向用户显示 `evidence_boundary=demo_only_not_physical_sensor`。
7. LLM、消息总线或工具失败时明确报告失败；不得声称任务或提醒已成功执行。

## Cron example

```json
{
  "name": "mooncat-demo-check",
  "schedule_type": "every",
  "interval_s": 2700,
  "message": "[DEMO] MoonCat scheduled policy check",
  "channel": "system",
  "chat_id": "mooncat-coach",
  "action": "mooncat_coach_tick",
  "action_args": "{\"mode\":\"execute\",\"source\":\"simulated\",\"valid\":true,\"workout_active\":false,\"do_not_disturb\":false,\"inactivity_minutes\":45,\"sleep_debt_minutes\":0,\"stress_score\":20,\"battery_percent\":80}"
}
```

## Example

用户：“看看月薪喵现在建议我做什么。”

→ `mooncat_coach_tick` with `mode=preview`, `source=simulated`

→ “演示策略建议伸展 60 秒。该结果来自 simulated 数据，不是 Gemini S1 传感器实测。”

## Scheduler constraints

- 官方 `cron_service` 每 10 秒检查一次到期任务，所以触发存在最多约一个检查周期的延迟，并非硬实时秒级。
- cron 在持有内部 mutex 时调用 action；`mooncat_coach_tick` 及其依赖不得调用 `cron_add`、`cron_list` 或 `cron_remove`，否则可能自锁。
- cron 会在 action 之后无条件发送 job 的 `message`。示例把框架回执放在 `system` channel；只有 Tool 的动态建议进入 `mooncat` UI channel。interval 设为 2700 秒，message 保留 `[DEMO]`，避免把内部检查冒充真实健康事件。
