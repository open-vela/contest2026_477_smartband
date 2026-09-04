from __future__ import annotations

import json
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def test_tool_uses_shared_policy_and_message_bus_with_cooldown() -> None:
    source = (ROOT / "agent" / "tool_mooncat_coach.c").read_text(encoding="utf-8")

    assert '#include "mooncat_coach_policy.h"' in source
    assert "mooncat_coach_evaluate(&observation)" in source
    assert "message_bus_push_outbound(&message)" in source
    assert '#define MOONCAT_CHANNEL "mooncat"' in source
    assert "g_mooncat_last_delivery_monotonic" in source
    assert "cooldown_active" in source
    assert "[DEMO]" not in source  # The shared policy owns user-facing wording.


def test_registry_overlay_matches_current_cron_and_tool_contract() -> None:
    patch = (ROOT / "agent" / "packages_ai_agent_overlay.patch").read_text(
        encoding="utf-8"
    )

    assert "src/tools/mooncat_coach_policy.c" in patch
    assert "src/tools/tool_mooncat_coach.c" in patch
    assert "diff --git a/Makefile b/Makefile" in patch
    assert 'mooncat_skill_builtin.h' in patch
    assert '"mooncat-active-coach", MOONCAT_SKILL_BUILTIN' in patch
    assert '"mooncat_coach_tick"' in patch
    assert 'TOOL_PARAM_ENUM("mode"' in patch
    assert 'TOOL_PARAM_ENUM("source"' in patch
    assert "tool_mooncat_coach_execute" in patch


def test_skill_is_flat_and_uses_real_cron_schema() -> None:
    skill = ROOT / "skills" / "mooncat-active-coach.md"
    assert skill.is_file()
    assert skill.parent == ROOT / "skills"
    text = skill.read_text(encoding="utf-8")

    assert text.startswith("# 月薪喵主动恢复教练\n")
    assert "schedule_type` 只能是 `every` 或 `at`" in text
    assert "`action_args` 必须是字符串化 JSON" in text
    assert "source=simulated" in text
    assert "demo_only_not_physical_sensor" in text
    assert "trigger_epoch" not in text
    assert '"schedule_type": "once"' not in text

    match = re.search(r"## Cron example\n\n```json\n(.*?)\n```", text, re.S)
    assert match is not None
    cron = json.loads(match.group(1))
    assert cron["schedule_type"] == "every"
    assert isinstance(cron["action_args"], str)
    action_args = json.loads(cron["action_args"])
    assert action_args["mode"] == "execute"
    assert action_args["source"] == "simulated"
    assert cron["channel"] == "system"
    assert cron["action"] == "mooncat_coach_tick"


def test_quickapp_uses_supported_velaclaw_ask_contract_only() -> None:
    source = (ROOT / "quickapp" / "mooncat-active-coach.js").read_text(
        encoding="utf-8"
    )

    assert "import velaclaw from '@system.velaclaw'" in source
    assert "velaclaw.ask({" in source
    assert "res.reply" in source
    assert "tool_calls" not in source
    assert "extra_info" not in source
    assert "created: false" in source
    assert "not Gemini S1 physical sensor evidence" in source


def test_cron_bootstrap_fixture_uses_stringified_action_args() -> None:
    fixture = json.loads(
        (ROOT / "examples" / "mooncat-coach-cron.json").read_text(encoding="utf-8")
    )

    assert fixture["schedule_type"] == "every"
    assert fixture["interval_s"] >= 2700
    assert fixture["channel"] == "system"
    assert fixture["action"] == "mooncat_coach_tick"
    assert fixture["message"].startswith("[DEMO]")
    assert isinstance(fixture["action_args"], str)
    action_args = json.loads(fixture["action_args"])
    assert action_args["mode"] == "execute"
    assert action_args["source"] == "simulated"
