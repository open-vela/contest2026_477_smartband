/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

/*
 * Runtime bootstrap copy.  skill_loader writes this flat file to
 * /data/agent/skills/mooncat-active-coach.md on first start.
 */
#define MOONCAT_SKILL_BUILTIN \
    "# 月薪喵主动恢复教练\n\n" \
    "决赛应用流程使用 mooncat_app_control，来源 app_event，仅为用户操作和应用计时。" \
    "focus_on/focus_off 只控制计时。schedule 需要稳定正整数 request_id、delay(1..86400秒)、" \
    "普通提醒也可用 remind(delay,duration)，同参数活动任务去重，完成后再次请求创建新任务。" \
    "duration(5..3600秒)；重试保留同一请求参数。list 返回任务 id/revision；" \
    "start/snooze/pause_today/complete/cancel 需要实际 id 和 revision，snooze 另需 delay。" \
    "complete 只表示计时结束后的用户确认。dnd_on/dnd_off、workout_on/workout_off、" \
    "resume_today 修改用户设置。queued 不是 shown；shown 只表示 UI 构建卡片。" \
    "storage_failed/stale/busy/queue_full 不得描述为成功。新任务依赖 Native UI 每秒检查，" \
    "UI 未运行则不出卡；到期读取当前状态，勿扰/运动/今天暂停时延后5分钟。" \
    "应用计时不得转换为实测久坐。以下是原有 simulated 演示接口，任务列表与新接口分开。\n\n" \
    "使用确定性端侧策略评估明确来源的手环状态。首个里程碑只接受 " \
    "simulated 演示数据，不能描述为真实传感器测量。\n\n" \
    "## When to use\n" \
    "当用户查询恢复建议、演示主动恢复教练或设置久坐任务时使用。\n\n" \
    "## How to use\n" \
    "1. 仅查询时调用 mooncat_coach_tick，mode=preview、source=simulated。\n" \
    "2. 现场演示时使用 mode=execute；只有 delivered 才代表消息已推送。\n" \
    "3. 主动任务用 cron_add，action=mooncat_coach_tick；action_args 必须是" \
    "字符串化 JSON。cron channel 使用 system，动态建议由 Tool 自己推送到 " \
    "mooncat channel。\n" \
    "4. noop 表示无建议或仍在 cooldown；error 必须如实报告。\n" \
    "5. 始终保留 [DEMO] 和 demo_only_not_physical_sensor 证据边界。\n" \
    "6. cron action 在 cron mutex 内执行，不得从 action 调用 cron API。\n"
