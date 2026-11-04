"""Compile real core/service sources; clocks are injected, no waiting for timers."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]


def test_offline_worker_and_nonblocking_ui_transport(tmp_path):
    cc = shutil.which("gcc")
    if not cc:
        pytest.skip("POSIX C compiler required")
    binary = tmp_path / "async-test"
    build = subprocess.run([
        cc, "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror", "-pthread",
        f'-DMOONCAT_APP_DIRECTORY="{tmp_path / "store"}"',
        "-I", str(ROOT / "common"), "-I", str(ROOT / "agent"),
        str(ROOT / "common/mooncat_app_state.c"), str(ROOT / "tests/host/test_app_async.c"),
        "-o", str(binary)], capture_output=True, text=True)
    assert build.returncode == 0, build.stderr
    run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
    assert run.returncode == 0, run.stdout + run.stderr


def test_checkpoint_io_and_failure_semantics(tmp_path):
    cc = shutil.which("gcc") or shutil.which("clang")
    if not cc:
        pytest.skip("POSIX C compiler required")
    binary = tmp_path / "checkpoint-test"
    build = subprocess.run([
        cc, "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror", "-pthread",
        f'-DMOONCAT_APP_DIRECTORY="{tmp_path / "store"}"',
        "-I", str(ROOT / "common"), "-I", str(ROOT / "agent"),
        str(ROOT / "common/mooncat_app_state.c"),
        str(ROOT / "tests/host/test_app_checkpoint.c"), "-o", str(binary)
    ], capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    run = subprocess.run([str(binary)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr


@pytest.fixture(scope="module")
def state_binary(tmp_path_factory):
    cc = shutil.which("gcc") or shutil.which("clang")
    if not cc:
        pytest.skip("GCC/Clang required; run under local Linux/WSL")
    target = tmp_path_factory.mktemp("app-core") / "state-test"
    subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(ROOT / "common"),
                    str(ROOT / "common/mooncat_app_state.c"),
                    str(ROOT / "tests/host/test_mooncat_app_state.c"),
                    "-o", str(target)], check=True, capture_output=True)
    return target


@pytest.mark.parametrize("scenario", range(1, 12))
def test_app_state(state_binary, scenario):
    run = subprocess.run([str(state_binary), str(scenario)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr


@pytest.fixture(scope="module")
def service_binary(tmp_path_factory):
    cc = shutil.which("gcc") or shutil.which("clang")
    cjson = os.environ.get("MOONCAT_CJSON_DIR")
    if not cc or not cjson:
        pytest.skip("GCC/Clang and MOONCAT_CJSON_DIR (cJSON.c/.h) required")
    temp = tmp_path_factory.mktemp("app-service")
    (temp / "tools").mkdir()
    shutil.copy(ROOT / "agent/tool_mooncat_coach.h", temp / "tools/tool_mooncat_coach.h")
    source = temp / "runner.c"
    source.write_text('''#include <stdio.h>
#include <string.h>
#include "mooncat_app_service.h"
#include "tools/tool_mooncat_coach.h"
int main(int argc,char **argv) {
    char output[4096]; struct mc_state state;
    if(argc!=2) return 2;
    if(!strcmp(argv[1],"poll")) { printf("%d\\n",mooncat_app_poll(&state));return 0; }
    tool_mooncat_app_execute(argv[1],output,sizeof(output));puts(output);return 0;
}
''', encoding="utf-8")
    target = temp / "service-test"
    subprocess.run([cc, "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-DMOONCAT_APP_NO_AUTOSTART", "-Wall", "-Wextra", "-Werror", "-pthread",
                    f'-DMOONCAT_APP_DIRECTORY="{temp / "store"}"',
                    "-I", str(ROOT / "common"), "-I", str(ROOT / "agent"), "-I", str(temp), "-I", cjson,
                    str(ROOT / "common/mooncat_app_state.c"), str(ROOT / "agent/mooncat_app_service.c"),
                    str(ROOT / "agent/tool_mooncat_app.c"), str(Path(cjson) / "cJSON.c"),
                    str(source), "-o", str(target)], check=True, capture_output=True)
    return target, temp / "store"


def test_tool_restart_and_storage_failures(service_binary):
    import json
    binary, store = service_binary
    def call(value):
        command = value if isinstance(value, str) else json.dumps(value)
        return json.loads(subprocess.check_output([str(binary), command], text=True))
    assert call({"op": "list"})["tasks"] == []
    request = {"op": "schedule", "request_id": 42, "delay": 100, "duration": 20}
    first = call(request)
    assert first["ok"] and first["source"] == "app_event"
    assert first["timebase"] == "app_monotonic"
    assert 0 <= first["tasks"][0]["due"] - first["now"] <= 100
    assert call(request)["status"] == "duplicate"
    task = call({"op": "list"})["tasks"][0]
    assert task["id"] == 1 and task["phase"] == 1
    assert task["duration"] == 20 and task["requested_delay"] == 100
    for bad in [{**request, "delay": 1.5}, {**request, "request_id": -1},
                {**request, "duration": 0}, {"op": "dnd_on", "typo": True},
                '{"op":"dnd_on","op":"dnd_off"}', {"op": "complete"},
                '{"op":"dnd_on"} garbage', {**request, "duration": 50}]:
        assert not call(bad)["ok"]
    assert call({"op": "cancel", "id": 1, "revision": 1})["ok"]
    assert call({"op": "cancel", "id": 1, "revision": 1})["status"] == "duplicate"
    reminder = {"op": "remind", "delay": 20, "duration": 60}
    assert call(reminder)["ok"]
    assert call(reminder)["status"] == "duplicate"
    assert len(call({"op": "list"})["tasks"]) == 2
    record = store / "app-state-v1.txt"
    good = record.read_bytes()
    record.write_bytes(good[:30])
    assert call({"op": "list"})["status"] == "storage_failed"
    assert record.read_bytes() == good[:30]
    record.write_bytes(good)
    assert call({"op": "list"})["ok"]
    # Force fopen(tmp) to fail, without relying on chmod under root/WSL mounts.
    (store / "app-state-v1.tmp").mkdir()
    assert call({"op": "dnd_on"})["status"] == "storage_failed"
    assert record.read_bytes() == good


def test_native_ui_feedback_and_worker_state(tmp_path):
    cc = shutil.which("gcc")
    cjson = os.environ.get("MOONCAT_CJSON_DIR")
    if not cc or not cjson:
        pytest.skip("POSIX GCC and MOONCAT_CJSON_DIR required")
    (tmp_path / "tools").mkdir()
    shutil.copy(ROOT / "agent/mooncat_app_service.h", tmp_path / "tools/mooncat_app_service.h")
    shutil.copy(ROOT / "agent/tool_mooncat_coach.h", tmp_path / "tools/tool_mooncat_coach.h")
    harness = tmp_path / "ui.c"
    source = ROOT / "firmware/openvela_ui/openvela_ui_coach.c"
    harness.write_text('''#include <stdio.h>
#include <time.h>
#include <stdatomic.h>
#include <stdint.h>
static _Atomic time_t fake_now=1800000000;
time_t mc_test_time(time_t *out) {if(out)*out=fake_now;return fake_now;}
uint64_t mooncat_test_monotonic(void) {return fake_now;}
''' + f'#include "{source.as_posix()}"\n' + '''
#include "mooncat_local_router.h"
#include "tools/tool_mooncat_coach.h"
int tool_registry_execute(const char *name,const char *input,char *output,size_t cap) {
    assert(!strcmp(name,"mooncat_app_control"));
    return tool_mooncat_app_execute(input,output,cap);
}
int mooncat_intent_model_predict(const char *text,struct mooncat_router_prediction_s *p) {
    (void)text;p->intent=MOONCAT_INTENT_COACH_SCHEDULE;p->confidence=0.99f;p->margin=0.9f;return 0;
}
static void ask(const char *text) {char *reply=mooncat_local_router_handle(text);assert(reply && strstr(reply,"true"));free(reply);}
static void click(uint32_t op) {lv_event_t event={(void *)(uintptr_t)op};action(&event);}
static void pump(void) {
    for(int i=0;i<300;i++) {
        struct timespec delay={0,10000000};
        timer->cb(timer);
        if(i>=25 && snapshot.last_now==(uint32_t)fake_now && !pending_ticket && workout_pending<0) return;
        nanosleep(&delay,NULL);
    }
    assert(!"UI service failed to settle");
}
static void *worker(void *unused) {
    struct mc_command c={0};(void)unused;c.op=MC_DND_ON;
    assert(mooncat_app_command(&c,NULL)==MC_OK);return NULL;
}
int main(void) {
    pthread_t thread; test_ui_thread=pthread_self();
    assert(openvela_ui_coach_start()==0);pump();
    toggle_panel(NULL);ask("开始专注");ask("20秒后提醒我休息");ask("20秒后提醒我休息");
    mooncat_app_snapshot(&snapshot);
    assert(snapshot.next_id==2);
    fake_now+=20;pump();
    assert(card && card_phase==MC_SHOWN && snapshot.shown_count==1);
    click(MC_START);pump();assert(card_phase==MC_RECOVERING);
    click(MC_COMPLETE);pump();assert(last_error==MC_TOO_EARLY);
    fake_now+=60;pump();click(MC_COMPLETE);pump();
    assert(!card && snapshot.completed_count==1);
    click(MC_SCHEDULE);pump();fake_now+=20;
    assert(!pthread_create(&thread,NULL,worker,NULL));pthread_join(thread,NULL);
    pump();assert(!card && snapshot.tasks[1].reason==MC_REASON_DND);
    click(MC_DND_OFF);pump();fake_now+=300;pump();assert(card_phase==MC_SHOWN);
    click(MC_START);pump();assert(card_phase==MC_RECOVERING);
    openvela_ui_coach_workout(1);fake_now++;pump();
    assert(!card && snapshot.workout && snapshot.tasks[1].phase==MC_CANCELLED);
    assert(snapshot.completed_count==1);
    openvela_ui_coach_stop();assert(!timer && !card && !panel && !launcher);
    mooncat_app_worker_stop();
    puts("native UI feedback and worker state passed");return 0;
}
''', encoding="utf-8")
    binary = tmp_path / "ui-test"
    run = subprocess.run([cc, "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-DMOONCAT_APP_TEST_CLOCK", "-Dtime=mc_test_time", "-Wall", "-Wextra",
                          "-Wno-unused-parameter", "-Werror", "-pthread",
                          f'-DMOONCAT_APP_DIRECTORY="{tmp_path / "store"}"',
                          "-I", str(ROOT / "tests/host/include/coach_ui"), "-I", str(ROOT / "common"),
                          "-I", str(ROOT / "agent"), "-I", str(tmp_path), "-I", cjson,
                          "-I", str(ROOT / "tests/host/include"),
                          str(ROOT / "common/mooncat_app_state.c"), str(ROOT / "agent/mooncat_app_service.c"),
                          str(ROOT / "agent/tool_mooncat_app.c"), str(ROOT / "agent/mooncat_local_router.c"), str(Path(cjson) / "cJSON.c"),
                          str(harness), "-o", str(binary)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr
    run = subprocess.run([str(binary)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr


def test_finals_patch_composes_with_existing_overlays(tmp_path):
    upstream = os.environ.get("MOONCAT_UPSTREAM_DIR")
    if not upstream:
        pytest.skip("MOONCAT_UPSTREAM_DIR: seven files from pinned packages_ai_agent required")
    files = ["CMakeLists.txt", "Makefile", "Kconfig", "src/tools/tool_registry.c",
             "src/tools/skill_loader.c", "src/core/agent_loop.c", "src/infra/vela_tls.c"]
    for file in files:
        target = tmp_path / file
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(Path(upstream) / file, target)
    subprocess.run(["git", "init", "-q", str(tmp_path)], check=True)
    patches = [ROOT / "agent" / ("packages_ai_agent_" + name + ".patch")
               for name in ["overlay", "http_chunked", "skill_route", "finals", "local_router"]]
    for patch in patches:
        run = subprocess.run(["git", "apply", "--ignore-space-change", str(patch)], cwd=tmp_path,
                             capture_output=True, text=True)
        assert run.returncode == 0, str(patch) + run.stdout + run.stderr
    # Reverse-check independently: apply scripts use this to avoid duplicate edits.
    for patch in patches:
        run = subprocess.run(["git", "apply", "--ignore-space-change", "--reverse", "--check", str(patch)],
                             cwd=tmp_path, capture_output=True, text=True)
        assert run.returncode == 0, str(patch) + run.stdout + run.stderr
