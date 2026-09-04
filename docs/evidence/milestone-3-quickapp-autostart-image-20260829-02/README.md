# Milestone 3：QuickApp 与 ai_agent 自启动候选

日期：2026-08-29（Asia/Shanghai）

## 结论与边界

本轮只修复两个已证实问题：让 `ai_agent` 随板级 `rcS` 开机启动；让
`nsh_minidisplay` defconfig 经过标准重配真正进入最终 `.config`，从而把
QuickApp/VAPP runtime 链接进固件。MoonCat Tool、native channel、产品 UI 和
已实机达到约 58 FPS 的 sparse-refresh/LCD stride 源码没有改动。

当前交付物仍是**静态审计通过、尚未烧录**的新候选。本文不声称它已经启动，
不声称 QuickApp 已在板端可用，也不声称新镜像仍达到约 58 FPS。PhoenixSuit 未
打开，设备未进入任何写入步骤。

## 根因与最小修复

旧构建虽然把赛事 overlay 合并进了
`vendor/allwinnertech/boards/r528/r528s3-gemini-s1/configs/nsh_minidisplay/defconfig`，
但最终 `nuttx/.config` 的 `CONFIG_BASE_DEFCONFIG` 仍是旧的
`openvela_ui_native-dirty`。上次仅运行增量 `make`，因此 QuickApp/VAPP 和 Feature
Framework 没有重新进入配置或最终固件。

修复包括：

- `firmware/board/rcS` 保持 `openvela_ui &`，并在
  `CONFIG_EXAMPLES_AI_AGENT_VELA` 保护下启动
  `ai_agent < /dev/null &`；stdin 脱离 NSH 控制台。
- `scripts/apply_overlay.sh` 同步并逐字节验证目标板 `rcS`。
- `scripts/build_gemini_s1_firmware.sh` 显式把
  `vendor/.../configs/nsh_minidisplay` 传给标准 `build.sh`，并在成功后硬性核对
  base defconfig、Feature Framework、QuickApp、VAPP、ai_agent 和 velaclaw。
- `tests/test_firmware_build_contract.py` 锁定上述构建与启动契约。

赛事仓验证为 `19 passed, 1 skipped`；`bash -n`、审计脚本编译与
`git diff --check` 均通过。最终本地记录见 `local-validation.txt`。

## hushen 构建证据

独立构建根：

`/data/openvela-contest-2026-gemini-s1-v1-build-20260829-02`

最终构建包装退出码为 `0`。第一次完整重配和链接本身成功，但旧包装器在
`savedefconfig` 后逐行复核规范化 defconfig 时误报；修正复核时机后重放成功，
两份日志均保留，最终采用 `quickapp-autostart-rebuild-20260829-01-retry1.log`。

| 产物 | bytes | SHA-256 |
|---|---:|---|
| `nuttx/.config` | 150,800 | `148B4CF4868AC399331B41474F5D947E4A025A8D09DB5C3D2D04789B4DCC7D31` |
| `nuttx/nuttx.elf` | 157,402,700 | `E019555B1768A8ECB4726B9065496E6201CE36610AB793537B79A34ABFDA8D56` |
| `nuttx/nuttx` | 7,631,060 | `3B836EE94BC4138DEC9AC7B69788113731154F6B8A54E393F8A5A0D7441593D8` |
| `nuttx/vela.bin` | 7,626,064 | `80B5F1D92F5925F6F081641D36D072A012C771681572B412668EFAD1C225AE02` |

最终 `.config` 已逐行确认：

- `CONFIG_BASE_DEFCONFIG="../vendor/.../configs/nsh_minidisplay"`
- `CONFIG_FEATURE_FRAMEWORK=y`
- `CONFIG_QUICKAPP=y`
- `CONFIG_QUICKAPP_VAPP=y`
- `CONFIG_EXAMPLES_AI_AGENT_VELA=y`
- `CONFIG_FEATURE_SYSTEM_VELACLAW=y`
- `CONFIG_LV_NUTTX_LCD_CUSTOM_BUFFER=y`
- `CONFIG_LV_NUTTX_LCD_BUFFER_SIZE=120`

最终 ELF 定义 `vapp_main`、`ai_agent_main`、`quickapp_mq_listener_task`、
`tool_mooncat_coach_execute` 和 `openvela_ui_main`。最终 `vela.bin` 同时包含
`hap://app/`、`vapp`、`ai_agent < /dev/null &` 以及 MoonCat Tool/Skill 锚点；生成的
`rcS` 同时包含 `openvela_ui &` 与受配置保护的 ai_agent 启动行。四个实验显示 BSP
禁词均不存在。

机器证据见 `remote-logs/quickapp-autostart-artifact-audit-20260829-01-retry1.txt`、
`remote-logs/quickapp-autostart-rebuild-20260829-01-retry1.log.txt`、
`remote-logs/final-config-audit.tsv` 和 `remote-logs/binary-anchor-audit.tsv`。

## IMAGEWTY 打包与交付物

独立打包根：

`/data/openvela-contest-2026-gemini-s1-v1-pack-20260829-02`

远端原件与赛事仓副本：

- hushen：
  `/data/openvela-contest-2026-gemini-s1-v1-pack-20260829-02/output/Gemini-S1-MoonCat-AI-QuickApp-autostart-20260829-candidate-02.img`
- 本地：
  `output/Gemini-S1-MoonCat-AI-QuickApp-autostart-20260829-candidate-02.img`
- 大小：`71,841,792 bytes`
- SHA-256：
  `8FEED521BDE963B34C02A94CF446A87DBEBECCAEB0E2B1CF4AD529EC42A6A1A3`

使用的已验证打包工具为：

- `lichee/tools/scripts/pack_img.sh`：
  `F95E3773A112AA2A2AF8511583372443960EE7893C67AF8F5B879EFAAB0506BF`
- `lichee/tools/tool/dragon`：
  `C8CF1B86D28BE549B741007DAD1BA83AC60CD3F932D0F13F02767F1B57D06132`

已实际运行 `pack_img.sh`。它生成了全部原始输入，但内置 dragon 步骤因系统缺少
`/lib/ld-linux.so.2` 退出 `1`；随后用任务私有 i386 runtime 运行**同一个** dragon，
退出 `0` 且日志包含 `Dragon execute image.cfg SUCCESS !`。没有全局安装兼容包。
`pack_img.sh` 临时生成的 `res/usrdata` 不属于锁定基线，最终 dragon 输入恢复了
基线 `res/Vres/usrdata/Vusrdata`；原始输入、最终输入、镜像和日志均保留。
对应记录为 `remote-logs/pack_img.log.txt`、`remote-logs/pack_img.exit`、
`remote-logs/dragon-final.log.txt` 和 `remote-logs/dragon-final.exit`。

## 22 条目与 V 文件审计

- Magic：`IMAGEWTY`
- Header version：`0x00000300`
- 条目数：`22`
- 22 项实际长度、16-byte padding、padding 零填充、镜像边界、非重叠、1 KiB
  offset 对齐及解包 payload 回读：全部 PASS
- `nsh.fex` 与最终 `vela.bin`：大小与 SHA-256 完全一致
- 解包工具校验与独立 uint32 little-endian 累加校验：三组全部 PASS

| 组 | payload offset / bytes | V offset / bytes | payload SHA-256 | V SHA-256 | uint32 校验 |
|---|---:|---:|---|---|---:|
| nsh / Vnsh | `0x1E6C00` / 7,626,064 | `0x92CC00` / 4 | `80B5F1D...5AE02` | `0F6BDCE8...F2FA` | 3,248,384,857 PASS |
| res / Vres | `0x92D000` / 8,851,456 | `0x119E000` / 4 | `59721E6D...0C06` | `70D1DE94...12E9` | 88,575,323 PASS |
| usrdata / Vusrdata | `0x119E400` / 53,366,784 | `0x4483400` / 4 | `4C6012A1...A89F` | `BE421E59...5D23` | 1,917,446,260 PASS |

完整边界和哈希见 `remote-logs/item-boundary-audit.tsv`、
`remote-logs/vfile-independent-audit.tsv`、`remote-logs/candidate-imagewty-info.txt`
与 `remote-logs/audit-gates.txt`。审计程序及其退出码也一并保留。

## 与两个已知镜像逐 payload 比较

与已点亮的 MoonCat AI 候选：

- 镜像从 `68,855,808` 增至 `71,841,792 bytes`，增加 `2,985,984 bytes`。
- `nsh.fex` 从 `4,639,888` 增至 `7,626,064 bytes`，增加 `2,986,176 bytes`。

与锁定回退基线：

- 镜像从 `68,699,136` 增至 `71,841,792 bytes`，增加 `3,142,656 bytes`。
- `nsh.fex` 从 `4,483,248` 增至 `7,626,064 bytes`，增加 `3,142,816 bytes`。

两次比较中，22 个 payload 都只有 `nsh.fex` 和它派生的 `Vnsh.fex` 不同；其余
20 项逐字节相同。镜像增长量比 `nsh` 字节长度增长量分别少 192 和 160 bytes，
原因是变长后的 `nsh` 到下一个 1 KiB 对齐条目的间隙相应缩短。后续
`Vnsh/res/Vres/usrdata/Vusrdata` 的容器 offset 整体后移，但 `res`、`usrdata`
及其 V payload 的长度和哈希没有变化。因此 payload 证据支持“QuickApp/Feature
Framework 与自启动进入系统固件，资源和用户数据保持基线”，但不能把重链接后的
每个 `nsh` 字节进一步归因到某一源码行。

逐项证据见 `remote-logs/payload-compare-vs-lit-candidate.tsv` 与
`remote-logs/payload-compare-vs-rollback.tsv`。

## 下一步门禁

当前未修改 PhoenixSuit 配置、未打开 PhoenixSuit、未烧录，也未执行任何设备写入。
下一步只能在用户另行明确授权后进行；授权需要绑定上述新候选的完整 SHA-256、升级
模式和是否允许清空 `/data`，且不得顺带授权 `/dev/boot0-direct`。烧录完成后仍需
分别验证启动/ADB、屏幕与触摸、QuickApp/AI 闭环，以及 logic/visual/render/flush
FPS、平均像素量和三类错误计数。
