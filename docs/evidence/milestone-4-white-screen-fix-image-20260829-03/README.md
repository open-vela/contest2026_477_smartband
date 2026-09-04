# Milestone 4：白屏修复与官方打包候选

日期：2026-08-29（Asia/Shanghai）

## 结论与证据边界

已确认白屏不是 NuttX、ADB、`openvela_ui` 或 `ai_agent` 未启动，而是两个
LVGL 前端同时占用 `/dev/lcd0` 和 SPI1。白屏设备的只读 `ps` 显示 PID 46 为
`luncher_mini`、PID 50 为 `openvela_ui`、PID 51 为 `ai_agent`；保留的 dmesg
在启动约 4.638 秒和 4.651 秒记录 PID 46、50 先后成功打开 `/dev/lcd0`，随后
两者交替触发 `spi_dma_tx_complete` / `spi_cpu_complete` timeout，最终出现
`SPI ERR! status 472`。

最小修复是在赛事配置 fragment 中加入：

```text
# CONFIG_LUNCHER_MINI_APP is not set
```

这样保留 QuickApp/VAPP、Feature Framework、`openvela_ui`、`ai_agent` 和既有
sparse-refresh/LCD stride 源码，只关闭会争抢 LCD 的 stock mini launcher。

本候选尚未烧录。本文只报告构建和镜像静态审计，不声称修复后固件已经启动、
QuickApp 已实机可用或仍达到约 58 FPS。

## 构建

独立构建根：

`/data/openvela-contest-2026-gemini-s1-v1-build-20260829-03`

按用户指示，新树复制并核对完成后删除了旧
`/data/openvela-contest-2026-gemini-s1-v1-build-20260829-02`，释放约 15 GB；
本地赛事仓和锁定回退镜像没有删除或覆盖。复制保留的 63 个指向旧绝对路径的
构建符号链接全部改指向新根，63/63 新目标存在。第一次重配在该修复前退出 1，
失败日志保留；修复后用标准 `build.sh nsh_minidisplay -j16` 完整重配、编译、
链接、`savedefconfig`，最终包装退出 0。

| 产物 | bytes | SHA-256 |
|---|---:|---|
| `nuttx/.config` | 150,685 | `7A146A6D011981E850F8C8EAECE8E61DCAB8B4E5F92AAA3D44DB945BA877BD12` |
| `nuttx/nuttx.elf` | 157,352,452 | `8F597690FFB1449E5153A999BB561216B608F656804708FF2984BBD63A4F2F14` |
| `nuttx/nuttx` | 7,627,060 | `54E39F1F84C524C506134ACA1B01BCB3C446F058110C216483B7B7EFDE50AB96` |
| `nuttx/vela.bin` | 7,622,064 | `229E9B0723070DE160074FA5FA810BF39F8BE19787ADEB321B29D500D4F3E8E0` |
| 预处理后的 `rcS` | 656 | `0F73A95C1BC28D6DB12ADCC4757ACB536BE776F600056C0DE267940C406F0BB3` |

最终 `.config` 硬性确认：

- `CONFIG_FEATURE_FRAMEWORK=y`
- `CONFIG_QUICKAPP=y`
- `CONFIG_QUICKAPP_VAPP=y`
- `CONFIG_EXAMPLES_AI_AGENT_VELA=y`
- `CONFIG_FEATURE_SYSTEM_VELACLAW=y`
- `CONFIG_LV_NUTTX_LCD_CUSTOM_BUFFER=y`
- `CONFIG_LV_NUTTX_LCD_BUFFER_SIZE=120`
- `# CONFIG_LUNCHER_MINI_APP is not set`
- `CONFIG_LUNCHER_MINI_APP=y` 不存在

预处理后的实际 `rcS` 只含 `openvela_ui &` 和 `ai_agent < /dev/null &`，不含
`luncher_mini`。ELF 中定义 `vapp_main`、`ai_agent_main`、局部
`quickapp_mq_listener_task`、`tool_mooncat_coach_execute` 和
`openvela_ui_main`，不定义 `luncher_mini_main`。`vela.bin` 中保留
`hap://app/`、`vapp`、ai_agent 自启动及 MoonCat Tool/Skill 锚点；launcher
启动/入口锚点和四个实验 BSP 禁词均为 0。

机器证据见 `remote-build-logs/`。

## 官方打包链

独立打包根：

`/data/openvela-contest-2026-gemini-s1-v1-pack-20260829-03`

实际使用：

- `lichee/tools/scripts/pack_img.sh`：
  `F95E3773A112AA2A2AF8511583372443960EE7893C67AF8F5B879EFAAB0506BF`
- 同一 `lichee/tools/tool/dragon`：
  `C8CF1B86D28BE549B741007DAD1BA83AC60CD3F932D0F13F02767F1B57D06132`

`pack_img.sh` 使用已验证的
`sun8iw20p1 / rtos / r528s3-gemini-s1 / nuttx /
r528s3/gemini-s1_nand` 参数生成全部官方输入。其内置 dragon 因宿主缺少
`/lib/ld-linux.so.2` 退出 1，原始输出已保留；随后恢复锁定回退基线的
`res/Vres/usrdata/Vusrdata`，通过任务私有 i386 runtime 调用同一个官方
`dragon`，退出 0，日志含 `Dragon execute image.cfg SUCCESS !`。没有安装全局
兼容包，也没有用 imagewty repack 代替官方 dragon。

## 候选交付物

- hushen 原件：
  `/data/openvela-contest-2026-gemini-s1-v1-pack-20260829-03/output/Gemini-S1-MoonCat-AI-QuickApp-autostart-no-launcher-20260829-candidate-03.img`
- 赛事仓副本：
  `output/Gemini-S1-MoonCat-AI-QuickApp-autostart-no-launcher-20260829-candidate-03.img`
- 大小：`71,837,696 bytes`
- SHA-256：
  `B5CC555A66247AFFE85399A6FFAFA5E5C70B2EE442AA94DA233A8DDE1CD73500`

远端 dragon 原件、远端 `output/` 副本和本地赛事仓副本大小与完整哈希相同。

## IMAGEWTY 与 payload 审计

- Magic：`IMAGEWTY`
- Header version：`0x00000300`
- 条目数：`22`
- 全部 22 项的实际长度、16-byte padding、零填充、镜像边界、非重叠、
  1 KiB offset 对齐及解包回读：PASS
- `nsh.fex` 与最终 `vela.bin` 大小、哈希和内容相同
- 安全解包工具校验与独立 uint32 little-endian 累加校验：三组均 PASS

| 组 | payload offset / bytes | V offset / bytes | payload SHA-256 | V SHA-256 | uint32 校验 |
|---|---:|---:|---|---|---:|
| nsh / Vnsh | `0x1E6C00` / 7,622,064 | `0x92BC00` / 4 | `229E9B07...3E8E0` | `4AEC9EDA...D3C35` | 1,906,807,655 PASS |
| res / Vres | `0x92C000` / 8,851,456 | `0x119D000` / 4 | `59721E6D...0C06` | `70D1DE94...12E9` | 88,575,323 PASS |
| usrdata / Vusrdata | `0x119D400` / 53,366,784 | `0x4482400` / 4 | `4C6012A1...AA89F` | `BE421E59...5D23` | 1,917,446,260 PASS |

相对白屏候选 `candidate-02`：镜像缩小 4,096 bytes，`nsh.fex` 缩小
4,000 bytes；后续 1 KiB 对齐间隙再缩小 96 bytes。相对锁定回退基线：镜像
增加 3,138,560 bytes，`nsh.fex` 增加 3,138,816 bytes；对齐间隙抵消 256
bytes。两次逐项比较都只有 `nsh.fex` 和派生的 `Vnsh.fex` 不同，其余 20 项
长度和 SHA-256 全部相同。由此可把 payload 差异限定为系统固件重链接和 Vnsh
更新，不能把重链接后的每个字节进一步归因到某一源码行。

完整机器证据见 `remote-pack-logs/`。

## 下一步门禁

本轮只做只读设备检查、构建、打包和静态审计；没有修改 PhoenixSuit 配置、没有
打开 PhoenixSuit、没有进入写入步骤。当前板上仍是白屏候选，不能用它证明新候选
的显示、QuickApp 或 FPS。

下一步只差用户在原任务或新任务中明确授权再次烧录，并把授权绑定到上述完整
SHA-256、升级模式、是否允许清空 `/data`，以及明确不包含
`/dev/boot0-direct`。烧录后仍需分别验证启动/ADB、显示和触摸、QuickApp/AI
闭环，以及 logic/visual/render/flush FPS、平均 flush pixels 和错误计数。
