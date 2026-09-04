from __future__ import annotations

import hashlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_locked_source_hashes() -> None:
    manifest = ROOT / "docs" / "evidence" / "baseline-source-sha256.txt"
    for line in manifest.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        expected, relative = line.split(maxsplit=1)
        source = ROOT / relative
        assert source.is_file(), relative
        assert hashlib.sha256(source.read_bytes()).hexdigest() == expected


def test_stride_fix_uses_actual_lvgl_stride() -> None:
    source = (ROOT / "firmware" / "lvgl" / "lv_nuttx_lcd.c").read_text(
        encoding="utf-8"
    )
    assert "lcd->area.stride = lv_draw_buf_width_to_stride(" in source
    assert "lv_display_get_color_format(disp)" in source


def test_sparse_ui_retains_performance_counters() -> None:
    source = (ROOT / "firmware" / "openvela_ui" / "openvela_ui.c").read_text(
        encoding="utf-8"
    )
    for marker in (
        "cat_compose_and_invalidate",
        "lv_obj_invalidate_area",
        "visual_frames",
        "dirty_pixels",
        "missed_ticks",
    ):
        assert marker in source
