import os
from pathlib import Path
import shutil
import subprocess
import tempfile

import pytest


ROOT = Path(__file__).resolve().parents[1]


def test_pure_c_policy_host_harness() -> None:
    compiler = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    if os.name == "nt" and compiler is None:
        compiler = shutil.which("cl")
    if compiler is None:
        pytest.skip("local C compiler unavailable; the same harness runs on hushen")

    temp_root = ROOT / ".test-tmp"
    temp_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=temp_root) as directory:
        executable = Path(directory) / ("mooncat_policy_test.exe" if os.name == "nt" else "mooncat_policy_test")
        command = [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            f"-I{ROOT / 'common'}",
            str(ROOT / "common" / "mooncat_coach_policy.c"),
            str(ROOT / "tests" / "host" / "test_mooncat_coach_policy.c"),
            "-o",
            str(executable),
        ]
        completed = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        assert completed.returncode == 0, completed.stdout + completed.stderr

        executed = subprocess.run([str(executable)], text=True, capture_output=True)
        assert executed.returncode == 0, executed.stdout + executed.stderr
        assert "all deterministic cases passed" in executed.stdout
