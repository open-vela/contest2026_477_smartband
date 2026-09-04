# Gemini S1 58 FPS 固件基线锁

记录日期：2026-08-29（Asia/Shanghai）

## 唯一锁定镜像

- 文件：`E:\C_Moved_From_C\Users\Lenovo\Desktop\schoolwork\_gemini_s1_8_3_fps48_sparse_lcdstridefix_image_20260815_06\Gemini-S1-8.3-WiFi-dedup-FPS48-sparse-lcdstridefixed-20260815.img`
- 大小：`68,699,136 bytes`
- SHA-256：`F42AC571713FD35FD04722DE01DFC3E60AF96D4C80DAA8709E81A214436C2BBC`

本仓不得覆盖、重命名或替换该镜像，也不得自行烧录设备。生成候选镜像与烧录
是两个独立授权边界。

## 源码闭包

| 作用 | 本仓文件 | 来源 SHA-256 |
|---|---|---|
| r7 稀疏合成/ARGB composite stride | `firmware/openvela_ui/openvela_ui.c` | `F13BEC3B9005B9DCD0F5DE325A675EE3848C2B364722F93AB7175EBC2448EB69` |
| 禁用虚假 VSYNC 等待 | `firmware/openvela_ui/openvela_ui_main.c` | `91C04782812751F963E769F8BF92EB79B37C1ABBF92B648B5D82020F9DA90DB4` |
| r8 LCD 真 stride | `firmware/lvgl/lv_nuttx_lcd.c` | `A5A1D4CFC9AC23F61B64184D493B493514B1D9ACC37A80DD9E952F1776F4C201` |
| Gemini S1 defconfig | `firmware/board/nsh_minidisplay.defconfig` | `FBFE347B5FDF4BE67DD1F2D0542E88B6C52CB70B81778975000B93C7C0080989` |

机器校验以 `docs/evidence/baseline-source-sha256.txt` 为准。

## 已知实机证据边界

既有、与锁定镜像同一源码链的板端测量为：动画逻辑约 57.88 FPS、不同视觉帧
约 57.91 FPS、render 约 58.90 FPS、flush 约 58.87 FPS，且当时未见 missed
tick、解码或 LCD ioctl 错误。这是历史已验证基线，不代表本仓之后的 AI 改动已
在实机验证。每个新候选都必须单独完成构建、容器审计和经授权的板端复验。
