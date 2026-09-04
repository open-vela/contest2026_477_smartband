import assert from "node:assert/strict"
import { spawnSync } from "node:child_process"
import { readFileSync } from "node:fs"
import { dirname, join } from "node:path"
import { fileURLToPath } from "node:url"

const root = dirname(fileURLToPath(import.meta.url))
const read = relativePath => readFileSync(join(root, relativePath), "utf8")

const canonicalHelper = read("mooncat-active-coach.js")
const packagedHelper = read("src/common/mooncat-active-coach.js")
const manifest = JSON.parse(read("src/manifest.json"))
const page = read("src/pages/index/index.ux")
const app = read("src/app.ux")

assert.equal(
  packagedHelper,
  canonicalHelper,
  "src/common helper must remain a byte-for-byte copy of mooncat-active-coach.js"
)

const syntax = spawnSync(process.execPath, ["--input-type=module", "--check"], {
  input: canonicalHelper,
  encoding: "utf8"
})
assert.equal(syntax.status, 0, syntax.stderr || "helper module syntax check failed")

assert.equal(manifest.package, "com.ov.mooncat")
assert.equal(manifest.router.entry, "pages/index")
assert.deepEqual(manifest.features, [{ name: "system.velaclaw" }])
assert.match(app, /export default/)

assert.match(canonicalHelper, /import velaclaw from ['"]@system\.velaclaw['"]/)
assert.match(canonicalHelper, /velaclaw\.ask\(\{[\s\S]*?query,/)
assert.match(canonicalHelper, /res\.reply/)
assert.match(canonicalHelper, /buildCoachQuery\(observation, mode = 'preview'\)/)
assert.match(canonicalHelper, /askCoach\(observation, mode = 'preview', callbacks = \{\}\)/)
assert.doesNotMatch(canonicalHelper, /\b(?:tool_calls|extra_info)\b/)
assert.equal((canonicalHelper.match(/@system\./g) || []).length, 1)

assert.match(page, /\[DEMO\] 模拟数据/)
assert.match(page, /一键现场演示/)
assert.match(page, /askCoach\(SIMULATED_OBSERVATION, "execute", \{/)
assert.match(page, /Skill → Tool → native UI/)
assert.match(page, /import \{ askCoach \} from "\.\.\/\.\.\/common\/mooncat-active-coach"/)
assert.doesNotMatch(page, /@(?!system\.velaclaw)[a-z]+\./)

console.log("quickapp static verification: PASS")
