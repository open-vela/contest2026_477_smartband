#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: $0 /path/to/smart-band-ui-redesign" >&2
  exit 2
fi

reference_root=$(realpath "$1")
asset_root="${reference_root}/src/common"
project_asset_root="$(cd "$(dirname "$0")" && pwd)/assets"
project_root="$(cd "$(dirname "$0")/../../.." && pwd)"
font_path="${OPENVELA_UI_FONT_PATH:-${project_root}/packages/demos/bandx/resource/font/assets/AlibabaPuHuiTi-3-55-Regular.ttf}"

test -f "${asset_root}/background.png"
test -f "${asset_root}/backgrounds/dark-purple.png"
test -f "${asset_root}/actions/toilet-break/frame-46.png"
test -f "${asset_root}/cat/frame-01.png"
test -f "${asset_root}/weather-icons/103.png"
test -f "${asset_root}/sport/running-icon.png"
test -f "${asset_root}/heart-rate/frame-01.png"
test -f "${asset_root}/music/player-logo.png"
test -f "${asset_root}/music/icons/play.png"
test -f "${asset_root}/music/tracks/track-03.mp3"
test -f "${asset_root}/notifications/bell.png"
test -f "${font_path}"
command -v gst-launch-1.0 >/dev/null

adb wait-for-device

# The openvela emulator adbd supports file sync, but some versions close the
# standard `adb shell` command channel.  Stage the final directory locally and
# push it as a whole so deployment does not depend on a remote mkdir command.
staging_root=$(mktemp -d)
trap 'rm -rf -- "${staging_root}"' EXIT

mkdir -p "${staging_root}/openvela_ui/cat" \
  "${staging_root}/openvela_ui/backgrounds" \
  "${staging_root}/openvela_ui/actions" \
  "${staging_root}/openvela_ui/icons" \
  "${staging_root}/openvela_ui/weather-icons" \
  "${staging_root}/openvela_ui/music"
cp "${asset_root}/background.png" \
  "${staging_root}/openvela_ui/background.png"
cp "${asset_root}/cat/"*.png "${staging_root}/openvela_ui/cat/"
cp "${asset_root}/backgrounds/"*.png \
  "${staging_root}/openvela_ui/backgrounds/"
cp -a "${asset_root}/actions/." "${staging_root}/openvela_ui/actions/"
cp "${asset_root}/weather-icons/103.png" \
  "${staging_root}/openvela_ui/icons/weather.png"
cp "${asset_root}/weather-icons/"*.png \
  "${staging_root}/openvela_ui/weather-icons/"
cp "${asset_root}/sport/running-icon.png" \
  "${staging_root}/openvela_ui/icons/sport.png"
cp "${asset_root}/heart-rate/frame-01.png" \
  "${staging_root}/openvela_ui/icons/health.png"
cp "${asset_root}/music/player-logo.png" \
  "${staging_root}/openvela_ui/icons/music.png"
cp -a "${asset_root}/music/." \
  "${staging_root}/openvela_ui/music/"
cp -a "${project_asset_root}/music/." \
  "${staging_root}/openvela_ui/music/"

# The R528 audio driver accepts PCM but does not decode MP3.  Convert the
# reference tracks on the host so emulator deployment exercises the same
# direct NxPlayer path as the board image: signed 16-bit LE, 24 kHz, mono.
for track in track-01 track-02 track-03; do
  gst-launch-1.0 -q \
    filesrc location="${asset_root}/music/tracks/${track}.mp3" ! \
    mpegaudioparse ! mpg123audiodec ! audioconvert ! audioresample ! \
    'audio/x-raw,format=S16LE,channels=1,rate=24000,layout=interleaved' ! \
    filesink location="${staging_root}/openvela_ui/music/tracks/${track}.pcm"
done
rm -f -- "${staging_root}/openvela_ui/music/tracks/track-01.mp3" \
  "${staging_root}/openvela_ui/music/tracks/track-02.mp3" \
  "${staging_root}/openvela_ui/music/tracks/track-03.mp3"

cp "${asset_root}/notifications/bell.png" \
  "${staging_root}/openvela_ui/icons/notifications.png"
cp "${font_path}" "${staging_root}/openvela_ui/ui-font.ttf"

adb push "${staging_root}/openvela_ui" /data/

echo "Assets deployed to /data/openvela_ui"
