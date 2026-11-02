from pathlib import Path
import re


SOURCE = (
    Path(__file__).resolve().parents[1]
    / "firmware"
    / "openvela_ui"
    / "openvela_ui.c"
)


def source_text() -> str:
    return SOURCE.read_text(encoding="utf-8")


def test_target_pacing_is_exactly_60_updates_per_second() -> None:
    text = source_text()
    target = int(re.search(r"#define UI_MOTION_TARGET_FPS (\d+)U", text).group(1))
    assert target == 60
    # Actual timing helper, including delayed wakeups, is compiled in the
    # animation kernel test. Check that the UI passes its absolute epoch.
    assert 'now - g_ui.cat_cycle_started, UI_MOTION_TARGET_FPS)' in text


def test_interpolation_preserves_action_cycle_durations() -> None:
    text = source_text()
    actions = re.findall(
        r'\{"([^"]+)",\s*(\d+),\s*(\d+),\s*\d+,\s*NULL,\s*0,\s*0\}',
        text,
    )

    assert actions == [
        ("cat", "34", "3400"),
        ("shy-wave", "19", "2300"),
        ("phone-rest", "35", "4200"),
        ("toilet-break", "46", "2300"),
    ]
    assert "g_ui.cat_cycle_duration = duration;" in text
    assert "elapsed = lv_tick_elaps(g_ui.cat_cycle_started)" in text
    # Arithmetic and repeated-frame timing are compiled and exercised against
    # the production helper in test_animation_kernel.py.
    assert "mooncat_animation_sample(g_ui.active_frames, g_ui.cat_frame_count," in text
    assert "g_ui.cat_cycle_duration, elapsed, &sample)" in text


def test_motion_uses_sparse_composite_and_pauses_offscreen() -> None:
    text = source_text()

    assert text.count("lv_image_create(g_ui.cat_layer)") == 1
    assert "cat_composite_init" in text
    assert "cat_prepare_pair" in text
    assert "cat_compose_and_invalidate" in text
    assert "lv_obj_invalidate_area(g_ui.cat_anim, &dirty_area);" in text
    assert text.count("pause_cat_animation();") == 2
    assert "lv_animimg_create" not in text
    assert "lv_animimg_start" not in text
