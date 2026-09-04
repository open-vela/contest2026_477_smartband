from __future__ import annotations

import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
QUICKAPP = ROOT / "quickapp"


def test_quickapp_has_auditable_runnable_source_layout() -> None:
    required = [
        QUICKAPP / "package.json",
        QUICKAPP / "src" / "manifest.json",
        QUICKAPP / "src" / "app.ux",
        QUICKAPP / "src" / "pages" / "index" / "index.ux",
        QUICKAPP / "src" / "common" / "icon.svg",
    ]
    assert all(path.is_file() for path in required)

    manifest = json.loads((QUICKAPP / "src" / "manifest.json").read_text("utf-8"))
    assert manifest["features"] == [{"name": "system.velaclaw"}]
    assert manifest["router"]["entry"] == "pages/index"


def test_quickapp_node_static_verifier() -> None:
    completed = subprocess.run(
        ["node", str(QUICKAPP / "verify.mjs")],
        cwd=QUICKAPP,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    assert completed.returncode == 0, completed.stdout + completed.stderr
    assert "quickapp static verification: PASS" in completed.stdout


def test_quickapp_proactive_cron_verifier() -> None:
    completed = subprocess.run(
        ["node", str(QUICKAPP / "verify-proactive.mjs")],
        cwd=QUICKAPP,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    assert completed.returncode == 0, completed.stdout + completed.stderr
    assert "quickapp proactive cron verification: PASS" in completed.stdout


def test_quickapp_offers_immediate_and_proactive_live_demo_paths() -> None:
    helper = (QUICKAPP / "mooncat-active-coach.js").read_text("utf-8")
    page = (QUICKAPP / "src" / "pages" / "index" / "index.ux").read_text(
        "utf-8"
    )

    assert "buildCoachQuery(observation, mode = 'preview')" in helper
    assert "mode: coachMode(mode)" in helper
    assert 'askCoach(SIMULATED_OBSERVATION, "execute", {' in page
    assert "buildScheduledCoachQuery(observation)" in helper
    assert "get_current_time" in helper
    assert "cron_list" in helper
    assert "cron_add" in helper
    assert "CURRENT_EPOCH_PLUS_${PROACTIVE_DELAY_SECONDS}" in helper
    assert "不要立即调用 mooncat_coach_tick" in helper
    assert "scheduleCoach(SIMULATED_OBSERVATION, {" in page
    assert "秒后主动提醒" in page
    assert "Skill → Tool → native UI" in page
