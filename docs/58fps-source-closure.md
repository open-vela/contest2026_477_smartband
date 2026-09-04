# 58 FPS Gemini S1 源码闭包

## 结论

本项目的产品固件基线是文件名含 `FPS48`、实机测得约 58 FPS 的 r8 版本。
`48` 是原验收门槛，不是实际帧率或旧版本号。旧 `smart-band-demo` 只可作为交互、
健康卡片和 QuickApp 设计参考，不进入固件源码闭包。

Windows 上原先没有单一完整 Git 工作树；最终版本由基础产品包和三次受控增量组成。
本仓已把这组闭包固化为版本化 overlay：

1. 基础产品文件来自 `openvela_ui_Gemini_S1_fps_opt_20260814_01`；
2. `openvela_ui.c` 必须采用 r7 文件，SHA-256 为 `F13BEC3B...8EB69`；
3. `openvela_ui_main.c` 采用 r6 的禁用伪 VSYNC 入口，SHA-256 为
   `91C04782...90DB4`；
4. `lv_nuttx_lcd.c` 采用 r8 的真实区域 stride 修复，SHA-256 为
   `A5A1D4CF...4C201`；
5. 产品 defconfig 为 `FBFE347B...80989`，赛事 AI 差异另存为小型 fragment，
   不篡改这份基线。

较早的 r6 `openvela_ui.c`（`6C2DECB9...`）缺少 ARGB8888 composite buffer 的
真实 stride 处理，不能作为最终版本。

## 三次性能修复的边界

- r6：60 Hz 运动节拍、预解码/合成、像素变化包围盒、稀疏 invalidation、120 行
  partial buffer、性能计数，并禁用伪 VSYNC 等待；
- r7：通过 `lv_draw_buf_width_to_stride(..., ARGB8888)` 分配并逐行访问 composite
  buffer；
- r8：在 `LCDDEVIO_PUTAREA` 前把 LVGL 的真实区域 stride 传给 LCD 驱动，消除
  partial refresh 行跨度错误。

## 三方一致性核验

2026-08-29 已逐文件核验：本仓四个锁定核心文件、Windows 原始 r6/r7/r8 文件、
以及 hushen 的历史最终构建树
`/data/openvela-ui-gemini-s1-fps48-lcdstridefix-20260815-r8` 哈希完全一致。远端
构建树中的最终镜像也与本地锁定镜像同为
`F42AC571...C2BBC`，`vela.bin` 为 `92FD8EF1...670C1`。

基础包其余 13 个构建文件同样与 hushen 最终树逐字节一致，完整机器记录见
`docs/evidence/upstream-lock.json`。

## 可复现性边界

hushen 的 r8 树能证明历史构建与最终镜像的闭环，但其 `.git` 链接、部分符号链接
和旧运行时依赖已经失效，因此不能声称“当前可从零 clean build”。本仓用显式
allowlist 和哈希锁保存产品差异；下一步必须把 overlay 应用到从官方
`dev-ai-contest-2026` manifest 初始化的干净树，再执行无污染构建。旧 r8 树在该
clean build 成功前仍是只读证据源，不属于可永久清理项。
