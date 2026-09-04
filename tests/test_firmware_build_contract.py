from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_rcs_autostarts_agent_without_console_input() -> None:
    text = (ROOT / "firmware" / "board" / "rcS").read_text(encoding="utf-8")

    assert text.count("ai_agent < /dev/null &") == 1
    assert "#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA\nai_agent < /dev/null &\n#endif" in text
    assert text.index("openvela_ui &") < text.index("ai_agent < /dev/null &")


def test_build_uses_and_verifies_target_defconfig() -> None:
    text = (ROOT / "scripts" / "build_gemini_s1_firmware.sh").read_text(
        encoding="utf-8"
    )

    assert 'board_config="vendor/allwinnertech/boards/r528/r528s3-gemini-s1/configs/nsh_minidisplay"' in text
    assert './build.sh "$board_config" "-j$jobs"' in text
    assert text.count('"$script_dir/apply_overlay.sh" --apply "$target_root"') == 2
    assert 'CONFIG_BASE_DEFCONFIG="../vendor/allwinnertech/boards/r528/r528s3-gemini-s1/configs/nsh_minidisplay"' in text
    for setting in (
        "CONFIG_FEATURE_FRAMEWORK=y",
        "CONFIG_QUICKAPP=y",
        "CONFIG_QUICKAPP_VAPP=y",
        "# CONFIG_LUNCHER_MINI_APP is not set",
        "CONFIG_EXAMPLES_AI_AGENT_VELA=y",
        "CONFIG_FEATURE_SYSTEM_VELACLAW=y",
    ):
        assert setting in text
