#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "usage: $0 --check|--apply|--verify OPENVELA_ROOT" >&2
  exit 2
}

[ "$#" -eq 2 ] || usage
mode=$1
target_root=$2
case "$mode" in
  --check|--apply|--verify) ;;
  *) usage ;;
esac

[ -d "$target_root" ] || {
  echo "missing openVela root: $target_root" >&2
  exit 3
}

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
target_root=$(CDPATH= cd -- "$target_root" && pwd)

agent_target="$target_root/packages/ai_agent"
agent_tools_target="$agent_target/src/tools"
defconfig_target="$target_root/vendor/allwinnertech/boards/r528/r528s3-gemini-s1/configs/nsh_minidisplay/defconfig"
tflm_target="$target_root/apps/mlearning/tflite-micro/tflite-micro"
router_patch="$repo_root/agent/packages_ai_agent_local_router.patch"
router_fragment="$repo_root/firmware/board/nsh_minidisplay-local-router.fragment"

router_files=(
  mooncat_local_router.c
  mooncat_local_router.h
  mooncat_intent_model.cc
  mooncat_intent_model_data.h
)

require_file() {
  [ -f "$1" ] || {
    echo "missing required file: $1" >&2
    exit 4
  }
}

verify_same() {
  cmp -s -- "$1" "$2" || {
    echo "local-router overlay mismatch: $2" >&2
    exit 20
  }
}

verify_route_order() {
  python3 - "$agent_target/src/core/agent_loop.c" <<'PY'
from pathlib import Path
import sys

text = Path(sys.argv[1]).read_text(encoding="utf-8")
function = text.index("static char* handle_nl_fast_path")
table = text.index("for (int i = 0; s_intents[i].keywords; i++)", function)
weather = text.index("/* Weather (special: needs city extraction) */", function)
music = text.index("/* Music play (special: needs keyword extraction) */", function)
local = text.index("return mooncat_local_router_handle(text);", function)
guard = text.index("tool_guard_check_injection(msg.content)", local)
dispatch = text.index("reply = handle_nl_fast_path(msg.content);", guard)
if not table < weather < music < local < guard < dispatch:
    raise SystemExit("local-router order contract failed")
print("local-router order verified: guard -> existing fast paths -> model -> LLM")
PY
}

check_layout() {
  [ -d "$agent_tools_target" ] || {
    echo "missing ai_agent tools destination: $agent_tools_target" >&2
    exit 5
  }
  [ -d "$tflm_target" ] || {
    echo "missing TFLite Micro source tree: $tflm_target" >&2
    exit 6
  }
  require_file "$defconfig_target"
  require_file "$agent_target/CMakeLists.txt"
  require_file "$agent_target/Kconfig"
  require_file "$agent_target/Makefile"
  require_file "$agent_target/src/core/agent_loop.c"
  require_file "$agent_tools_target/tool_mooncat_coach.c"
  require_file "$router_patch"
  require_file "$router_fragment"
  require_file "$script_dir/merge_defconfig.py"

  local filename
  for filename in "${router_files[@]}"; do
    require_file "$repo_root/agent/$filename"
  done

  grep -Fq 'src/tools/tool_mooncat_coach.c' "$agent_target/CMakeLists.txt" || {
    echo "existing MoonCat overlay is not present in ai_agent CMakeLists.txt" >&2
    exit 7
  }
  grep -Fq 'mooncat_coach_tick' "$agent_target/src/tools/tool_registry.c" || {
    echo "existing MoonCat tool registration is not present" >&2
    exit 8
  }
}

patch_is_applied() {
  git -C "$agent_target" apply --reverse --check "$router_patch" >/dev/null 2>&1
}

patch_is_ready() {
  git -C "$agent_target" apply --check "$router_patch" >/dev/null 2>&1
}

check_layout

if [ "$mode" = "--check" ]; then
  if patch_is_applied; then
    patch_state=applied
  elif patch_is_ready; then
    patch_state=ready
  else
    echo "local-router patch neither applies nor matches target: $agent_target" >&2
    exit 9
  fi
  echo "local-router target verified: $target_root (patch: $patch_state)"
  exit 0
fi

if [ "$mode" = "--verify" ]; then
  for filename in "${router_files[@]}"; do
    verify_same "$repo_root/agent/$filename" "$agent_tools_target/$filename"
  done
  patch_is_applied || {
    echo "local-router patch is not applied: $agent_target" >&2
    exit 21
  }
  python3 "$script_dir/merge_defconfig.py" check \
    "$defconfig_target" "$router_fragment"
  verify_route_order
  echo "local-router overlay content verified: $target_root"
  exit 0
fi

for filename in "${router_files[@]}"; do
  install -m 0644 "$repo_root/agent/$filename" "$agent_tools_target/$filename"
done

if ! patch_is_applied; then
  git -C "$agent_target" apply --check "$router_patch"
  git -C "$agent_target" apply "$router_patch"
fi

python3 "$script_dir/merge_defconfig.py" apply \
  "$defconfig_target" "$router_fragment"

"$0" --verify "$target_root"
echo "local-router overlay applied without network access or cleanup: $target_root"
