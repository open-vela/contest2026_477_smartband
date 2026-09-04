import assert from "node:assert/strict"
import { readFileSync } from "node:fs"
import { dirname, join } from "node:path"
import { fileURLToPath } from "node:url"

const root = dirname(fileURLToPath(import.meta.url))
const read = relativePath => readFileSync(join(root, relativePath), "utf8")
const canonicalHelper = read("mooncat-active-coach.js")
const page = read("src/pages/index/index.ux")

const testableHelper = canonicalHelper.replace(
  /import velaclaw from ['"]@system\.velaclaw['"]/,
  'const velaclaw = { ask(options) { return options } }'
)
const helper = await import(
  `data:text/javascript;base64,${Buffer.from(testableHelper).toString("base64")}`
)
const observation = {
  valid: true,
  workoutActive: false,
  doNotDisturb: false,
  inactivityMinutes: 75,
  sleepDebtMinutes: 40,
  stressScore: 72,
  batteryPercent: 68,
}
const scheduledQuery = helper.buildScheduledCoachQuery(observation)

assert.equal(helper.proactiveDelaySeconds, 20)
assert.match(scheduledQuery, /get_current_time/)
assert.match(scheduledQuery, /cron_list/)
assert.match(scheduledQuery, /cron_add/)
assert.match(scheduledQuery, /不要立即调用 mooncat_coach_tick/)

const templateMatch = scheduledQuery.match(/任务模板：(\{.*\})/)
assert.ok(templateMatch, "scheduled query must carry one JSON job template")
const scheduledJob = JSON.parse(templateMatch[1])
assert.equal(scheduledJob.name, "mooncat-demo-once")
assert.equal(scheduledJob.schedule_type, "at")
assert.equal(scheduledJob.at_epoch, "CURRENT_EPOCH_PLUS_20")
assert.equal(scheduledJob.channel, "system")
assert.equal(scheduledJob.action, "mooncat_coach_tick")
assert.equal(typeof scheduledJob.action_args, "string")
assert.deepEqual(JSON.parse(scheduledJob.action_args), {
  mode: "execute",
  source: "simulated",
  valid: true,
  workout_active: false,
  do_not_disturb: false,
  inactivity_minutes: 75,
  sleep_debt_minutes: 40,
  stress_score: 72,
  battery_percent: 68,
})
assert.equal(helper.scheduleCoach(observation).query, scheduledQuery)

assert.match(page, /立即提醒/)
assert.match(page, /秒后主动提醒/)
assert.match(page, /askCoach\(SIMULATED_OBSERVATION, "execute", \{/)
assert.match(page, /scheduleCoach\(SIMULATED_OBSERVATION, \{/)
assert.match(page, /from "\.\.\/\.\.\/common\/mooncat-active-coach"/)

console.log("quickapp proactive cron verification: PASS")
