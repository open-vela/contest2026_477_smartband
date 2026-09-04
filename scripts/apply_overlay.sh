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

ui_target="$target_root/packages/demos/openvela_ui"
agent_target="$target_root/packages/ai_agent"
agent_tools_target="$agent_target/src/tools"
lcd_target="$target_root/apps/graphics/lvgl/lvgl/src/drivers/nuttx/lv_nuttx_lcd.c"
defconfig_target="$target_root/vendor/allwinnertech/boards/r528/r528s3-gemini-s1/configs/nsh_minidisplay/defconfig"
board_rcs_target="$target_root/vendor/allwinnertech/boards/r528/r528s3-gemini-s1/src/etc/init.d/rcS"

ui_files=(
  CMakeLists.txt
  Kconfig
  Make.defs
  Makefile
  openvela_ui.c
  openvela_ui.h
  openvela_ui_agent_bridge.c
  openvela_ui_agent_bridge.h
  openvela_ui_main.c
  openvela_ui_main_mooncat.c
  openvela_ui_sport.c
  openvela_ui_sport.h
  openvela_ui_sync.c
  openvela_ui_sync.h
  openvela_ui_timesync.c
  openvela_ui_timesync.h
  openvela_ui_weather.c
  openvela_ui_weather.h
)

agent_files=(
  mooncat_skill_builtin.h
  tool_mooncat_coach.c
  tool_mooncat_coach.h
)

common_files=(
  mooncat_coach_policy.c
  mooncat_coach_policy.h
)

board_base="$repo_root/firmware/board/nsh_minidisplay.defconfig"
board_fragment="$repo_root/firmware/board/nsh_minidisplay-ai-contest.fragment"
board_rcs_source="$repo_root/firmware/board/rcS"
baseline_manifest="$repo_root/docs/evidence/baseline-source-sha256.txt"
agent_patch="$repo_root/agent/packages_ai_agent_overlay.patch"
skill_source="$repo_root/skills/mooncat-active-coach.md"
skill_target="$agent_target/agent_skills/mooncat-active-coach.md"

require_file() {
  [ -f "$1" ] || {
    echo "missing required file: $1" >&2
    exit 4
  }
}

verify_same() {
  local source=$1
  local target=$2
  cmp -s -- "$source" "$target" || {
    echo "overlay mismatch: $target" >&2
    exit 20
  }
}

check_layout() {
  [ -d "$ui_target" ] || {
    echo "missing UI destination: $ui_target" >&2
    exit 5
  }
  [ -d "$agent_tools_target" ] || {
    echo "missing ai_agent tools destination: $agent_tools_target" >&2
    exit 6
  }
  require_file "$lcd_target"
  require_file "$defconfig_target"

  local filename
  for filename in "${ui_files[@]}"; do
    require_file "$repo_root/firmware/openvela_ui/$filename"
  done
  for filename in "${agent_files[@]}"; do
    require_file "$repo_root/agent/$filename"
  done
  for filename in "${common_files[@]}"; do
    require_file "$repo_root/common/$filename"
  done
  require_file "$repo_root/firmware/lvgl/lv_nuttx_lcd.c"
  require_file "$board_base"
  require_file "$board_fragment"
  require_file "$board_rcs_source"
  require_file "$baseline_manifest"
  require_file "$script_dir/merge_defconfig.py"
  require_file "$agent_patch"
  require_file "$skill_source"
  (cd "$repo_root" && sha256sum -c "${baseline_manifest#"$repo_root"/}" >/dev/null)
}

patch_is_applied() {
  git -C "$agent_target" apply --reverse --check "$agent_patch" >/dev/null 2>&1
}

check_layout

if [ "$mode" = "--check" ]; then
  if patch_is_applied; then
    patch_state=applied
  elif git -C "$agent_target" apply --check "$agent_patch" >/dev/null 2>&1; then
    patch_state=ready
  else
    echo "ai_agent patch neither applies nor matches target: $agent_target" >&2
    exit 7
  fi
  echo "overlay target layout verified: $target_root (ai_agent patch: $patch_state)"
  exit 0
fi

if [ "$mode" = "--verify" ]; then
  for filename in "${ui_files[@]}"; do
    verify_same "$repo_root/firmware/openvela_ui/$filename" "$ui_target/$filename"
  done
  verify_same "$repo_root/firmware/lvgl/lv_nuttx_lcd.c" "$lcd_target"
  for filename in "${agent_files[@]}"; do
    verify_same "$repo_root/agent/$filename" "$agent_tools_target/$filename"
  done
  for filename in "${common_files[@]}"; do
    verify_same "$repo_root/common/$filename" "$agent_tools_target/$filename"
  done
  verify_same "$skill_source" "$skill_target"
  verify_same "$board_rcs_source" "$board_rcs_target"
  python3 "$script_dir/merge_defconfig.py" check "$defconfig_target" "$board_fragment"
  patch_is_applied || {
    echo "ai_agent source patch is not applied: $agent_target" >&2
    exit 21
  }
  echo "overlay content verified: $target_root"
  exit 0
fi

for filename in "${ui_files[@]}"; do
  install -m 0644 "$repo_root/firmware/openvela_ui/$filename" "$ui_target/$filename"
done
if [ -d "$repo_root/firmware/openvela_ui/assets" ]; then
  mkdir -p "$ui_target/assets"
  cp -a "$repo_root/firmware/openvela_ui/assets/." "$ui_target/assets/"
fi
install -m 0644 "$repo_root/firmware/lvgl/lv_nuttx_lcd.c" "$lcd_target"

for filename in "${agent_files[@]}"; do
  install -m 0644 "$repo_root/agent/$filename" "$agent_tools_target/$filename"
done
for filename in "${common_files[@]}"; do
  install -m 0644 "$repo_root/common/$filename" "$agent_tools_target/$filename"
done

mkdir -p "$agent_target/agent_skills"
install -m 0644 "$skill_source" "$skill_target"

if ! patch_is_applied; then
  git -C "$agent_target" apply --check "$agent_patch"
  git -C "$agent_target" apply "$agent_patch"
fi

install -m 0644 "$board_base" "$defconfig_target"
python3 "$script_dir/merge_defconfig.py" apply "$defconfig_target" "$board_fragment"
install -m 0644 "$board_rcs_source" "$board_rcs_target"

"$0" --verify "$target_root"
echo "overlay applied without deleting unrelated target files: $target_root"
