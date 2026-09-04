# openvela UI native shell

This directory is the isolated native LVGL implementation of the smart-band
reference under `openvela_ui/smart-band-ui-redesign`. The reference project is
read-only: its Quick App source is not compiled into this application.

The native shell implements the six-page circular trunk:

```text
Home <-> Weather <-> Sport <-> Health <-> Music <-> Notifications <-> Home
```

AI Assistant is intentionally excluded. Swipe down on Home to open Appearance,
then choose Background or Action. Swipe up to return. The five background
themes and six cat actions use the reference assets, while the native UI keeps
two shared `lv_image` objects and only builds paths for the selected action.
The two images cross-fade adjacent source frames on a 16/17/17 ms cadence,
averaging 60 visual updates per second while preserving every action's original
cycle duration and authored hold timing. The timer pauses while a full-screen
module is open, so the extra blending work is limited to the visible cat region.
The Appearance flow is a full-screen overlay, so vertical navigation does not
compete with the horizontal TileView.

Runtime assets are isolated below the configured data root:

```text
backgrounds/  five 432x514 themes
actions/      five additional frame sequences
cat/          default cover-dance sequence
icons/        primary-page markers
music/        player icons and 24 kHz mono S16LE PCM tracks
```

Music playback uses NxPlayer directly with `/dev/audio/pcm0p`; it does not
require `mediad`.  `deploy_assets.sh` converts the reference MP3 tracks to raw
PCM with GStreamer before pushing them, matching the files carried by the
R528 board images.  The host therefore needs `gst-launch-1.0` together with
the `mpegaudioparse` and `mpg123audiodec` plugins.

Enable `CONFIG_LVX_USE_DEMO_OPENVELA_UI`, build the image, start the emulator,
then deploy runtime assets:

```sh
./packages/demos/openvela_ui/deploy_assets.sh /path/to/smart-band-ui-redesign
adb shell 'openvela_ui &'
```
