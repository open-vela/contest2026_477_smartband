import os
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile

import pytest


ROOT = Path(__file__).resolve().parents[1]


def _msvc_vcvars() -> Path | None:
    if os.name != "nt":
        return None

    visual_studio = (
        Path(os.environ.get("ProgramFiles", r"C:\Program Files"))
        / "Microsoft Visual Studio"
        / "2022"
    )
    candidates = sorted(
        visual_studio.glob("*/VC/Auxiliary/Build/vcvars64.bat"), reverse=True
    )
    return candidates[0] if candidates else None


def _load_training_module():
    path = ROOT / "local_router" / "train_router.py"
    spec = importlib.util.spec_from_file_location("mooncat_train_router", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_local_router_host_harness() -> None:
    compiler = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    vcvars = _msvc_vcvars() if compiler is None else None
    if compiler is None and vcvars is None:
        pytest.skip("local C compiler unavailable; run the same harness on hushen")

    temp_root = ROOT / ".test-tmp"
    temp_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=temp_root) as directory:
        executable = Path(directory) / (
            "mooncat_local_router_test.exe"
            if os.name == "nt"
            else "mooncat_local_router_test"
        )
        sources = [
            str(ROOT / "agent" / "mooncat_local_router.c"),
            str(ROOT / "tests" / "host" / "test_mooncat_local_router.c"),
        ]
        if compiler is not None:
            command = [
                compiler,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                f"-I{ROOT / 'agent'}",
                f"-I{ROOT / 'tests' / 'host' / 'include'}",
                *sources,
                "-o",
                str(executable),
            ]
            use_shell = False
        else:
            cl_command = [
                "cl",
                "/nologo",
                "/std:c11",
                "/W4",
                "/WX",
                "/utf-8",
                f"/I{ROOT / 'agent'}",
                f"/I{ROOT / 'tests' / 'host' / 'include'}",
                *sources,
                f"/Fe:{executable}",
            ]
            command = (
                f"call {subprocess.list2cmdline([str(vcvars)])} >nul && "
                f"{subprocess.list2cmdline(cl_command)}"
            )
            use_shell = True
        completed = subprocess.run(
            command,
            cwd=directory,
            shell=use_shell,
            text=True,
            encoding="mbcs" if compiler is None else "utf-8",
            capture_output=True,
        )
        assert completed.returncode == 0, completed.stdout + completed.stderr

        executed = subprocess.run([str(executable)], text=True, capture_output=True)
        assert executed.returncode == 0, executed.stdout + executed.stderr
        assert "all host behavior cases passed" in executed.stdout

        training = _load_training_module()
        parity_cases = [
            "abc123def",
            "abc１２３def",
            "12 34",
            "1a2",
            "时间20秒",
            "A９ 88",
        ]
        for text in parity_cases:
            runtime = subprocess.run(
                [str(executable), "--features-hex", text.encode("utf-8").hex()],
                text=True,
                encoding="ascii",
                capture_output=True,
            )
            assert runtime.returncode == 0, runtime.stdout + runtime.stderr
            actual = [int(value) for value in runtime.stdout.strip().split(",") if value]
            expected = training.np.flatnonzero(training.featurize(text)).tolist()
            assert actual == expected, text


def test_router_contract_sources_are_present() -> None:
    source = (ROOT / "agent" / "mooncat_local_router.c").read_text(encoding="utf-8")
    assert "mooncat_local_router_handle" in source
    assert "mooncat_parse_epoch" in source
    assert "CURRENT_EPOCH_PLUS_" in source
    assert "mooncat-demo-once" in source

    model = (ROOT / "agent" / "mooncat_intent_model.cc").read_text(
        encoding="utf-8"
    )
    assert "MOONCAT_ROUTER_LABEL_COUNT" not in model
    assert "MOONCAT_ROUTER_CONFIDENCE_THRESHOLD" not in model
    assert "MOONCAT_ROUTER_MARGIN_THRESHOLD" not in model
    assert "static_assert(MOONCAT_INTENT_MODEL_FEATURE_COUNT" in model
    assert "static_assert(MOONCAT_INTENT_MODEL_LABEL_COUNT" in model
