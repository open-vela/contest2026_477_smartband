#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "usage: $0 OPENVELA_ROOT [JOBS]" >&2
  exit 2
}

[ "$#" -ge 1 ] && [ "$#" -le 2 ] || usage
target_root=$1
jobs=${2:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}
case "$jobs" in
  ''|*[!0-9]*|0) echo "JOBS must be a positive integer: $jobs" >&2; exit 2 ;;
esac

[ -d "$target_root" ] || {
  echo "missing openVela root: $target_root" >&2
  exit 3
}

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
target_root=$(CDPATH= cd -- "$target_root" && pwd)
board_config="vendor/allwinnertech/boards/r528/r528s3-gemini-s1/configs/nsh_minidisplay"
final_config="$target_root/nuttx/.config"

bash "$script_dir/apply_overlay.sh" --apply "$target_root"
bash "$script_dir/apply_local_router_overlay.sh" --apply "$target_root"

(
  cd "$target_root"
  ./build.sh "$board_config" "-j$jobs"
)

[ -f "$final_config" ] || {
  echo "missing final configuration: $final_config" >&2
  exit 30
}

require_config() {
  grep -Fqx -- "$1" "$final_config" || {
    echo "final config missing required setting: $1" >&2
    exit 31
  }
}

require_config 'CONFIG_EXAMPLES_AI_AGENT_VELA=y'
require_config 'CONFIG_FEATURE_SYSTEM_VELACLAW=y'
require_config 'CONFIG_SYSTEM_FLATBUFFERS=y'
require_config 'CONFIG_MATH_GEMMLOWP=y'
require_config 'CONFIG_MATH_KISSFFT=y'
require_config 'CONFIG_MATH_RUY=y'
require_config 'CONFIG_TFLITEMICRO=y'
require_config 'CONFIG_AI_AGENT_LOCAL_TOOL_ROUTER=y'
require_config 'CONFIG_LV_USE_SNAPSHOT=y'

# build.sh may save a minimized defconfig back to the board directory. Restore
# both auditable overlays after the final config has passed the build gates.
bash "$script_dir/apply_overlay.sh" --apply "$target_root"
bash "$script_dir/apply_local_router_overlay.sh" --apply "$target_root"

echo "Gemini S1 local-router firmware built and config-gated: $target_root"
