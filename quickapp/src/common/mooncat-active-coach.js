// SPDX-License-Identifier: Apache-2.0

import velaclaw from '@system.velaclaw'

const BOUNDARY = 'simulated demo data; not Gemini S1 physical sensor evidence'
const PROACTIVE_DELAY_SECONDS = 20
const PROACTIVE_JOB_NAME = 'mooncat-demo-once'

function integerInRange(value, minimum, maximum, name) {
  if (!Number.isInteger(value) || value < minimum || value > maximum) {
    throw new Error(`${name} must be an integer from ${minimum} to ${maximum}`)
  }
  return value
}

function coachMode(value) {
  const mode = value || 'preview'
  if (mode !== 'preview' && mode !== 'execute') {
    throw new Error('mode must be preview or execute')
  }
  return mode
}

function coachPayload(observation, mode) {
  return {
    mode: coachMode(mode),
    source: 'simulated',
    valid: observation.valid === true,
    workout_active: observation.workoutActive === true,
    do_not_disturb: observation.doNotDisturb === true,
    inactivity_minutes: integerInRange(
      observation.inactivityMinutes, 0, 65535, 'inactivityMinutes'),
    sleep_debt_minutes: integerInRange(
      observation.sleepDebtMinutes, 0, 65535, 'sleepDebtMinutes'),
    stress_score: integerInRange(observation.stressScore, 0, 100, 'stressScore'),
    battery_percent: integerInRange(
      observation.batteryPercent, 0, 100, 'batteryPercent'),
  }
}

export function buildCoachQuery(observation, mode = 'preview') {
  const payload = coachPayload(observation, mode)

  return [
    '请使用“月薪喵主动恢复教练”Skill，并务必调用 mooncat_coach_tick 一次，不要只生成文字建议。',
    `输入 JSON：${JSON.stringify(payload)}`,
    `证据边界：${BOUNDARY}。`,
    '请在最终回复中简短说明工具返回的 status，并保留 [DEMO] 标识。',
  ].join('\n')
}

export function buildScheduledCoachQuery(observation) {
  const payload = coachPayload(observation, 'execute')
  const job = {
    name: PROACTIVE_JOB_NAME,
    schedule_type: 'at',
    at_epoch: `CURRENT_EPOCH_PLUS_${PROACTIVE_DELAY_SECONDS}`,
    message: '[DEMO] MoonCat proactive check completed',
    channel: 'system',
    chat_id: 'mooncat-coach',
    action: 'mooncat_coach_tick',
    action_args: JSON.stringify(payload),
  }

  return [
    '请使用“月薪喵主动恢复教练”Skill，设置一次现场主动提醒演示。',
    '先调用 get_current_time 获取当前 epoch，再调用 cron_list 检查同名任务。',
    `若已有未到期的 ${PROACTIVE_JOB_NAME}，不要重复创建，直接报告现有任务。`,
    `否则调用 cron_add，并把 at_epoch 设为当前 epoch + ${PROACTIVE_DELAY_SECONDS}（必须是数字）。`,
    `任务模板：${JSON.stringify(job)}`,
    '不要立即调用 mooncat_coach_tick；到期后只让 cron action 调用一次。',
    `证据边界：${BOUNDARY}。`,
    '最终回复只说明任务是否创建、预计触发窗口和 [DEMO] 边界。',
  ].join('\n')
}

function askAgent(query, callbacks) {

  return velaclaw.ask({
    query,
    success(res) {
      if (callbacks.success) callbacks.success(res.reply)
    },
    fail(data, code) {
      if (callbacks.fail) callbacks.fail({ data, code, created: false })
    },
    complete() {
      if (callbacks.complete) callbacks.complete()
    },
  })
}

export function askCoach(observation, mode = 'preview', callbacks = {}) {
  return askAgent(buildCoachQuery(observation, mode), callbacks)
}

export function scheduleCoach(observation, callbacks = {}) {
  return askAgent(buildScheduledCoachQuery(observation), callbacks)
}

export const evidenceBoundary = BOUNDARY
export const proactiveDelaySeconds = PROACTIVE_DELAY_SECONDS
