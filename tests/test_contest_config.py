from __future__ import annotations

import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "firmware" / "board" / "nsh_minidisplay.defconfig"
FRAGMENT = ROOT / "firmware" / "board" / "nsh_minidisplay-ai-contest.fragment"
MERGER = ROOT / "scripts" / "merge_defconfig.py"


def load_merger():
    spec = importlib.util.spec_from_file_location("merge_defconfig", MERGER)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_contest_fragment_is_small_and_explicit() -> None:
    lines = FRAGMENT.read_text(encoding="utf-8").splitlines()
    settings = {line for line in lines if line.startswith(("CONFIG_", "# CONFIG_"))}

    assert 'CONFIG_EXAMPLES_AI_AGENT_VELA=y' in settings
    assert 'CONFIG_EXAMPLES_AI_AGENT_VELA_DATA_DIR="/data/agent"' in settings
    assert 'CONFIG_EXAMPLES_AI_AGENT_VELA_PROGNAME="ai_agent"' in settings
    assert 'CONFIG_EXAMPLES_AI_AGENT_VELA_STACKSIZE=32768' in settings
    assert 'CONFIG_EXAMPLES_AI_AGENT_VELA_SHELL_ALLOWLIST=y' in settings
    assert 'CONFIG_FEATURE_SYSTEM_VELACLAW=y' in settings
    assert 'CONFIG_MBEDTLS_NET_C=y' in settings
    assert 'CONFIG_SYSTEM_POPEN=y' in settings
    assert 'CONFIG_SYSTEM_POPEN_STACKSIZE=16384' in settings
    assert 'CONFIG_SYSTEM_POPEN_PRIORITY=100' in settings
    assert 'CONFIG_MQ_MAXMSGSIZE=4096' in settings
    assert 'CONFIG_LV_USE_ANIMIMG=y' in settings
    assert 'CONFIG_LVX_USE_DEMO_OPENVELA_UI=y' in settings
    assert 'CONFIG_OPENVELA_UI_DATA_ROOT="/data/openvela_ui"' in settings
    assert 'CONFIG_OPENVELA_UI_HTTP_TIMESYNC=y' in settings
    for channel in ("LVGL_UI", "FEISHU", "WEIXIN", "MQTT", "NODE", "MCP"):
        assert f"# CONFIG_AI_AGENT_{channel} is not set" in settings
    assert len(settings) <= 37


def test_merge_preserves_58fps_product_config_and_is_idempotent() -> None:
    merger = load_merger()
    ordered, settings = merger.read_fragment(FRAGMENT)
    base = BASE.read_text(encoding="utf-8")
    merged = merger.merge(base, ordered, settings)

    assert "CONFIG_LV_NUTTX_LCD_CUSTOM_BUFFER=y" in merged
    assert "CONFIG_LV_NUTTX_LCD_BUFFER_SIZE=120" in merged
    assert "CONFIG_FEATURE_FRAMEWORK=y" in merged
    assert "CONFIG_QUICKAPP=y" in merged
    assert "# CONFIG_LUNCHER_MINI_APP is not set" in merged
    assert "CONFIG_LUNCHER_MINI_APP=y" not in merged
    assert "CONFIG_UORB=y" in merged
    assert "CONFIG_MQ_MAXMSGSIZE=1024" not in merged
    assert "CONFIG_MQ_MAXMSGSIZE=4096" in merged
    assert "CONFIG_MBEDTLS_NET_C=y" in merged
    assert "CONFIG_SYSTEM_POPEN=y" in merged
    assert "CONFIG_LVX_USE_DEMO_OPENVELA_UI=y" in merged
    assert "CONFIG_OPENVELA_UI_HTTP_TIMESYNC=y" in merged
    assert merger.merge(merged, ordered, settings) == merged


def test_locked_product_defconfig_stays_unmodified() -> None:
    text = BASE.read_text(encoding="utf-8")
    assert "CONFIG_MQ_MAXMSGSIZE=1024" in text
    assert "CONFIG_EXAMPLES_AI_AGENT_VELA=y" not in text
