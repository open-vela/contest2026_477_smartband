# Milestone 2：未烧录候选 IMG

日期：2026-08-29（Asia/Shanghai）

## 交付物

- hushen 远端原件：
  `/data/openvela-contest-2026-gemini-s1-v1-pack-20260829-01/output/Gemini-S1-MoonCat-AI-contest-20260829-candidate-01.img`
- 本地赛事仓副本：
  `output/Gemini-S1-MoonCat-AI-contest-20260829-candidate-01.img`
- PhoenixSuit 短路径副本：
  `E:\GeminiFlash\mooncat-ai-20260829-01.img`
- 大小：`68,855,808 bytes`
- SHA-256：
  `2A11F0E4FA94A622286EBEB7BF1108F9A7E9E41E6B1E23C424AD12DA95E6B1DA`

三份候选文件的大小和 SHA-256 相同；远端原件、打包输入和日志均保留在上述
独立打包根。赛事仓的 `output/` 不进入 Git。

锁定回退基线仍为：

- 归档路径：
  `E:\C_Moved_From_C\Users\Lenovo\Desktop\schoolwork\_gemini_s1_8_3_fps48_sparse_lcdstridefix_image_20260815_06\Gemini-S1-8.3-WiFi-dedup-FPS48-sparse-lcdstridefixed-20260815.img`
- PhoenixSuit 短路径：`E:\GeminiFlash\fps48-sparse-lcdstridefixed.img`
- 大小：`68,699,136 bytes`
- SHA-256：
  `F42AC571713FD35FD04722DE01DFC3E60AF96D4C80DAA8709E81A214436C2BBC`

本轮没有覆盖、改名或替换任一基线文件。

## 打包链

输入来自 hushen 成功构建副本
`/data/openvela-contest-2026-gemini-s1-v1-build-20260829-02`。新 `vela.bin` 为
`4,639,888 bytes`，SHA-256 为
`B249B331F1FFF3BA7AE9DEC85C386AF8BD620CA3BD475A933148CE8A204F885D`；复制到
lichee 的 `configs/nsh.fex` 后逐字节相同。

使用的 Gemini S1 打包工具为：

- `lichee/tools/scripts/pack_img.sh`：
  `F95E3773A112AA2A2AF8511583372443960EE7893C67AF8F5B879EFAAB0506BF`
- `lichee/tools/tool/dragon`：
  `C8CF1B86D28BE549B741007DAD1BA83AC60CD3F932D0F13F02767F1B57D06132`

`pack_img.sh` 使用已验证的 `sun8iw20p1 / rtos / r528s3-gemini-s1 / nuttx /
r528s3/gemini-s1_nand` 参数生成 IMAGEWTY 输入。它重新生成的 `res.fex` 和
`usrdata.fex` 与锁定基线不同，因此最终 `dragon` 前已恢复锁定基线中的
`res/Vres/usrdata/Vusrdata`。32 位 `dragon` 通过任务私有 i386 runtime 运行，
没有安装系统包或添加全局架构；最终 `dragon-final-retry1.exit` 为 `0`，日志含
`Dragon execute image.cfg SUCCESS !`。中间的 loader/插件失败日志也原样保留，
但它们没有产出或替换最终候选。

## IMAGEWTY 审计

- Magic：`IMAGEWTY`
- Header version：`0x00000300`
- 条目：`22`
- 全部 22 个条目的实际长度、padding、边界、非重叠和 1 KiB offset 对齐：PASS
- 解包工具校验与独立 uint32 little-endian 累加校验：三组均 PASS
- `nsh.fex` 与输入 `vela.bin`：逐字节相同
- 实验显示 BSP 禁词 `ILI9341_V2`、`BOOT_COLOR_SEQUENCE`、
  `PURE_COLOR_WRITE_SEQUENCE`、`SOFTWARE_SPI_GATE`：均不存在

| 组 | offset | 实际长度 | padded 长度 | payload SHA-256 | V SHA-256 | V 校验 |
|---|---:|---:|---:|---|---|---|
| nsh / Vnsh | `0x1E6C00` / `0x653C00` | `4,639,888` / `4` | `4,639,888` / `16` | `B249B331...F885D` | `21FADAF1...68369` | `3753806723`，PASS |
| res / Vres | `0x654000` / `0xEC5000` | `8,851,456` / `4` | `8,851,456` / `16` | `59721E6D...0C06` | `70D1DE94...B958` | `88575323`，PASS |
| usrdata / Vusrdata | `0xEC5400` / `0x41AA400` | `53,366,784` / `4` | `53,366,784` / `16` | `4C6012A1...AA89F` | `BE421E59...5D23` | `1917446260`，PASS |

相对锁定基线，22 个 payload 中只有 `nsh.fex` 和派生的 `Vnsh.fex` 哈希不同；
其余 20 项逐字节相同。`nsh.fex` 从 `4,483,248` 增至 `4,639,888` bytes，增加
`156,640` bytes；整镜像增加 `156,672` bytes，多出的 `32` bytes 来自后续条目
的对齐变化。候选 `nsh.fex` 精确等于已验证的新 AI 构建输出，且包含
`mooncat_coach_tick`、`mooncat-active-coach`、`MOONCAT ACTIVE COACH` 锚点。因此
打包层面发生的是预期的系统固件 payload 替换及其 V 校验更新，没有 boot、资源或
用户数据漂移；该结论不把重编译后的每个二进制字节进一步归因到某一源码行。

机器证据见 `remote-logs/candidate-imagewty-info.txt`、
`remote-logs/payload-compare.tsv`、`remote-logs/item-boundary-audit.tsv`、
`remote-logs/vfile-independent-audit.tsv` 和 `remote-logs/audit-gates.txt`。

## PhoenixSuit 已准备但未启动

- 程序：
  `E:\C_Moved_From_C\Users\Lenovo\Desktop\schoolwork\_isolated_zip_audit_20260726_1858\PhoenixSuit_Windows_2.0.0\PhoenixSuit.exe`
- 配置：同目录 `PhoenixSuit.cfg`
- 原配置备份：同目录
  `PhoenixSuit.cfg.pre-mooncat-ai-20260829-01.bak`
- 当前 `image` / `path_0`：`E:\GeminiFlash\mooncat-ai-20260829-01.img`
- `path_1` 回退：`E:\GeminiFlash\fps48-sparse-lcdstridefixed.img`
- 普通升级配置：`radioindex=1`、`erase enable=0`、`all=0`

配置前只读检查未发现运行中的 PhoenixSuit、VID `1F3A`/`18D1` 设备或
5037/5038 监听。`PhoenixSuit.cfg.before` 与 `PhoenixSuit.cfg.prepared` 是配置前后
快照。没有启动 PhoenixSuit、没有点击升级、没有进入写入流程、没有烧录设备。

## 获得“现在烧录”授权后的验收清单

1. 再次核对 GUI 选择文件、短路径候选大小和完整 SHA-256，并确认回退基线可读；
   同时重新检查 USB 模式。未得到本次明确授权前不点击升级。
2. 记录 PhoenixSuit 是否到达 100% / “固件烧写成功”及耗时；这只证明写入流程，
   不单独证明系统启动、显示或性能。
3. 验证设备离开 FEL，正常枚举并恢复 ADB；确认是 Gemini S1 实机后再使用
   `adb.exe -P 5038 -s 1234`，不把模拟器或其他端点当作手环。
4. 验证 native UI 正常、无白点/撕裂/黑屏；QuickApp 一键触发后，重放
   `@system.velaclaw → Skill → mooncat_coach_tick → mooncat channel → native UI`
   链路，确认 8 秒提醒卡与 QuickApp reply。
5. 分别记录动画 logic、不同 visual frame、render、flush、平均 flush pixels、
   missed ticks、decode errors 和 LCD ioctl errors。目标是复现历史约
   `57.88 / 57.91 / 58.90 / 58.87 FPS`，硬门槛不低于 `48 FPS`，三类错误仍为 0。
6. 当前演示输入仍是固定 75 分钟久坐模拟值；除非另有传感器证据，不声称是真实
   体征触发。

当前状态严格是“可烧录、未烧录”。候选的启动、AI 闭环和约 58 FPS 表现都等待
一次新的、明确的实机烧录授权和板端验收。
