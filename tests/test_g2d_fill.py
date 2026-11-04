"""Exercise the production G2D fill descriptor against a bounded DMA model."""
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]


@pytest.mark.parametrize("kind", ["fill", "image"])
def test_dma_format_pitch_and_clip(tmp_path, kind):
    cc = shutil.which("gcc")
    if not cc:
        pytest.skip("POSIX C compiler required")
    # Only replace platform includes; compile the actual production functions.
    filename = "lv_draw_g2d_fill.c" if kind == "fill" else "lv_draw_g2d_img.c"
    source = (ROOT / "firmware/lvgl" / filename).read_text()
    source = source.replace('#include "lv_draw_g2d.h"', "")
    source = source.replace("#include <g2d_driver.h>", "")
    (tmp_path / f"{kind}.c").write_text(source)
    harness = Path(__file__).with_name(f"g2d_{kind}_harness.c")
    binary = tmp_path / "probe"
    result = subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Wno-unused-parameter",
                             "-I", str(tmp_path), str(harness), "-o", str(binary)],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
