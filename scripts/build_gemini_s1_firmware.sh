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

# build.sh/configure.sh -e compares the requested defconfig with the previous
# nuttx/defconfig and performs the required distclean before copying it into
# nuttx/.config.  Passing the board explicitly is essential: a plain make here
# would silently retain the older openvela_ui_native-dirty configuration.
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

require_config 'CONFIG_BASE_DEFCONFIG="../vendor/allwinnertech/boards/r528/r528s3-gemini-s1/configs/nsh_minidisplay"'
require_config 'CONFIG_FEATURE_FRAMEWORK=y'
require_config 'CONFIG_QUICKAPP=y'
require_config 'CONFIG_QUICKAPP_VAPP=y'
require_config '# CONFIG_LUNCHER_MINI_APP is not set'
require_config 'CONFIG_EXAMPLES_AI_AGENT_VELA=y'
require_config 'CONFIG_FEATURE_SYSTEM_VELACLAW=y'
require_config 'CONFIG_LV_USE_SNAPSHOT=y'

# build.sh finishes with savedefconfig and copies its minimized result back to
# the board directory.  Restore the canonical, auditable overlay after the
# final .config and binary have already passed their gates.
bash "$script_dir/apply_overlay.sh" --apply "$target_root"
echo "Gemini S1 contest firmware built from nsh_minidisplay and verified: $target_root"
