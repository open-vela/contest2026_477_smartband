# Gemini S1 后台更新

2026-11-02 已在 R528 Gemini S1 上实际验证 OpenixCLI 0.2.4：FEL 初始化、FES 系统分区写入与校验、自动重启、ADB 回连、数据恢复。全程无需 PhoenixSuit 窗口。

工具来源：[OpenixCLI v0.2.4](https://github.com/YuzukiTsuru/OpenixCLI/releases/tag/v0.2.4)。Windows ZIP 内的 `openixcli.exe` 和 `libusb-1.0.dll` 放在同一目录。本机实测使用 `--backend winusb`，兼容当前 `usbUDisc` 全志驱动；没有更换系统驱动。

## 本板必须处理的数据恢复

实测 `--mode partition --partitions bootloader` 虽跳过 usrdata 载荷，重启后 `/data` 的原文件仍未出现。OpenixCLI 该模式仍下发 MBR、Boot0、Boot1；因此不能把“未下发 usrdata”解释为文件系统已保留。具体是哪一步触发 NAND 文件系统重建，尚未独立隔离。

`scripts/gemini_update.py` 将刷前完整备份、刷后恢复及逐文件回读比较合入后台流程。第二次更新已完整执行，318 个文件全部一致。不要单独复制一条裸 `openixcli flash` 命令代替这个流程。`keep_data` 尚未在本板验证，而且该版本仅按 `udisk/private/reserve` 名称跳过数据分区，不包含本板的 `usrdata`。

备份目录包含设备私有配置，仅留在本机；不要提交、上传或打印其中内容。

## 使用

先保持板子在正常 ADB 模式。指定新版 ADB 的可执行文件、端口和序列号，避免 PATH 中的旧 PhoenixSuit ADB。

```powershell
python scripts/gemini_update.py `
  --adb <platform-tools/adb.exe> --adb-port 5038 --serial 1234 `
  --openixcli <openixcli.exe> `
  --image <finals-current.img> --reference <candidate08.img> `
  --work-dir <本机新的更新记录目录>
```

不带 `--execute` 只审查镜像，不连接或修改板子。确认参数后加 `--execute` 执行完整流程。可从后台进程启动并重定向日志；无需 TTY、前台窗口或按键。

流程限制为相同 22 项 Gemini IMAGEWTY v3 容器，只接受 `nsh.fex` 和派生的 `Vnsh.fex` 变化，系统载荷不能超过 16 MiB。参考镜像和 Boot0 必须对应这块 Gemini 板，不能用于其他 Allwinner 板。执行前完成这些检查；执行中不设置可能中断 NAND 写入的主机超时。

脚本会暂停本板 `openvela_ui`、`ai_agent` 和 `kvdbd`，备份 `/data`，进入 FEL，扫描唯一设备，指定系统分区并启用写后校验，再恢复文件和重启。日志、私有备份及 `result.json` 均保存在指定目录；拒绝覆盖已有备份目录。如果流程失败，先查看保留的日志和板端模式，不要无条件重新烧录。

## 软件 FEL 入口

当前固件的 `adb reboot bootloader` 实测只会普通重启。正确入口已同时核对 R528 HAL 和原始 Boot0：

- `SUNXI_RTC_DATA_BASE = 0x07090100`，`RTC_FEL_INDEX = 2`。
- `EFEX_FLAG = 0x5aa5a55a`，对应地址 `0x07090108`。
- 原版 `boot0_nand.fex` 的 Thumb 指令在偏移 `0x1e22` 读取索引 2，在 `0x1e2a` 比较该标志。
- 经 NSH 写入并回读后普通重启，Windows 枚举 `1f3a:efe8`，OpenixCLI 成功握手；正常系统为 `18d1:4e11`。

脚本只写上述经过核对的启动标志，不使用 `dd` 覆盖 MTD。NAND 的擦除、坏块和校验处理交给板级 FES 下载程序。

## 构建和打包

hushen 仍使用 `/data/mooncat-closure-build05` 和 `/data/mooncat-closure-pack08`。仅更新现有工作镜像中的 `nsh.fex`、`Vnsh.fex`，用保留的官方 dragon 打包；历史 `output/...candidate-08.img` 不变。本轮逐载荷比较确认其余 20 项完全一致，未创建额外远端源码或打包副本。

性能计数器地址须与本次运行固件绑定。2026-11-02 08:39:34 编译版本的 ELF 符号为 UI `0x41b54f64`、LCD `0x41b50f30`；这不是后续构建可硬编码的通用地址。
