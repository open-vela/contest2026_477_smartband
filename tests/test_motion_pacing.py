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
    base = 1000 // target
    remainder = 1000 % target
    accumulator = 0
    periods = []

    for _ in range(target):
        period = base
        accumulator += remainder
        if accumulator >= target:
            accumulator -= target
            period += 1
        periods.append(period)

    assert target == 60
    assert set(periods) == {16, 17}
    assert sum(periods) == 1000
    assert accumulator == 0


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
    assert "phase = (uint64_t)elapsed * g_ui.cat_frame_count * 256U /" in text


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
