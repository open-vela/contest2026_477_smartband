# hushen 远端清理与隔离构建证据（2026-08-29）

## 1. 范围与结论

本记录只覆盖 SSH 配置主机 `ubuntu24-hushen` 的 `/data`。可复用连接前缀为：

```bash
ssh -o BatchMode=yes ubuntu24-hushen '<read-only command>'
```

本轮永久清理仅限第 2 节列出的 16 个、预先完成只读审计的非 Git 顶层目录。删除前每个目录的审计结果均为：顶层路径直接位于 `/data`、不是符号链接、目录树内无 `.git`、`lsof +D` 无打开文件、无 Docker bind mount、无外部符号链接指入。候选目录逻辑字节合计为 `5,528,447,317`。

删除执行命令的原始输出未保留在本报告作者可核验的日志中，因此这里不重构或猜测该命令。删除后于 `2026-08-29T07:37:20Z` 逐项复核，16 个精确路径全部不存在。未继续扩大清理范围。

## 2. 已删除的精确目录

### A. 旧构建、运行时和可重建证据

| 逻辑字节 | 绝对路径 |
| ---: | --- |
| 553,156,352 | `/data/gemini-s1-minidisplay-demo-20260806` |
| 569,413,582 | `/data/gemini-s1-minidisplay-v2-20260806` |
| 240,033,081 | `/data/smart-band-gemini-s1-vapp-candidate-20260730-r3` |
| 25,912,702 | `/data/smart-band-health-lab-rc-20260729T144202CST` |
| 335,840,835 | `/data/smart-band-live-goldfish-20260802-AhOhjV` |
| 1,077,974,303 | `/data/smart-band-simulator-unification-gdb-30778322567` |
| 1,012,460,090 | `/data/smart-band-simulator-unification-goldfish-gh30739969622` |
| 673,201,822 | `/data/smart-band-simulator-unification-goldfish-gh30744768473` |
| 741,391,218 | `/data/smart-band-simulator-unification-goldfish-gh30778322567` |
| 757,812 | `/data/smart-band-sync-coverage-20260726-runner-audit` |

A 组合计：`5,230,141,797` bytes。

### B. 已由后续版本覆盖的阶段源码快照

| 逻辑字节 | 绝对路径 |
| ---: | --- |
| 95,278,683 | `/data/smart-band-q1s-20260721T132034CST` |
| 44,396,915 | `/data/smart-band-q3-20260721T215900CST` |
| 7,446,971 | `/data/smart-band-q5-native-20260725T115000CST` |
| 17,623,079 | `/data/smart-band-q5-stress-20260725T225246CST` |
| 30,937,490 | `/data/smart-band-q8-rc-20260727T123000CST` |
| 102,622,382 | `/data/smart-band-q8-rc8h-20260727T150000CST` |

B 组合计：`298,305,520` bytes。相关产品源码里程碑已进入本地版本化 Git 历史；该删除也永久移除了这些远端快照随附的旧阶段 evidence。

## 3. 容量证据

- 删除前的较早只读快照（人类可读粒度）：`147G total / 77G used / 63G available / 55%`。
- 删除目录的 `du -sb` 逻辑总量：`5,528,447,317` bytes。逻辑字节不能直接当作文件系统块可用量差值。
- 删除后的即时快照（继承本轮共享执行记录）：`73,109,323,776` bytes available，`52%` 使用率。
- 隔离测试根创建后，本报告作者于 `2026-08-29T07:37:20Z` 复核：

```text
filesystem=/dev/sdb1
total=157394878464
used=76216778752
available=73108365312
capacity=52%
mount=/data
```

`df -h /data` 同次显示约 `147G total / 71G used / 69G available / 52%`。

## 4. 明确保留的边界

`2026-08-29T07:40:28Z` 复核以下目录全部仍存在：

- `/data/openvela-ui-gemini-s1-fps48-lcdstridefix-20260815-r8`
- `/data/gemini-s1-imagewty-safe-20260730-codex`
- `/data/gemini-s1-screen-fix-20260806`
- `/data/smart-band-gemini-s1-g0-20260726`
- `/data/clash-verge-remote`
- `/data/smart-band-q1c-20260720T223937CST`
- `/data/smart-band-live-goldfish-tmux-20260802-2xWz25`
- `/data/smart-band-simulator-unification-goldfish-gh30781794581`
- `/data/smart-band-ux-port-20260731T161911CST-58460710`

当前 QEMU PID `382976`、`382995`、`3392740`、`3392742`、`3392764` 均保持运行。`lsof` 汇总显示当前活动文件只落在 `clash-verge-remote`、`smart-band-q1c`、`smart-band-live-goldfish-tmux`、`smart-band-simulator-unification-goldfish-gh30781794581` 和 `smart-band-ux-port` 五个顶层目录。

未删除 Docker 数据、含 Git 元数据目录、未知目录或本地锁定固件；未烧录设备。

## 5. 可用构建根与依赖盘点

### 主机构建

现有隔离主机测试根：

```text
/data/openvela-contest-2026-gemini-s1-v1-work-20260829-01
```

盘点时大小约 `52K`，包含：

```text
common/mooncat_coach_policy.c
common/mooncat_coach_policy.h
tests/host/test_mooncat_coach_policy.c
mooncat_policy_test
```

主机环境：Ubuntu GCC/G++ `13.3.0`、CMake `3.28.3`、Ninja `1.11.1`、GNU Make `4.3`、Python `3.12.3`、Git `2.43.0`、Docker `29.1.3`；`16` 个逻辑 CPU，约 `31 GiB` 内存、`28 GiB` available。Node/npm 当前不在默认 `PATH`，所以该环境已经满足 C policy harness，但 QuickApp 工具链仍需单独锁定。

### 58 FPS 固件源与依赖链

58 FPS 只读源基线：

```text
/data/openvela-ui-gemini-s1-fps48-lcdstridefix-20260815-r8
```

该树约 `8.3G`，包含 73 个 `.git` 标记，不能作为比赛日常直接写入的构建工作区。它还通过顶层符号链接依赖 `/data/smart-band-gemini-s1-g0-20260726` 的 `build`、`external`、`frameworks`、`prebuilts` 和 `tests`。G0 的 GCC 预构建链接又解析到：

```text
/data/gemini-s1-screen-fix-20260806/toolchain-meta
```

其中 `arm-none-eabi-gcc` 为 GCC `13.4.0`。因此 `smart-band-gemini-s1-g0-20260726` 和 `gemini-s1-screen-fix-20260806` 虽然名称较旧，仍是当前 58 FPS 树的实际依赖，必须保留。

### 建议的隔离固件编译布局

后续完整固件编译应新建版本化工作根，例如：

```text
/data/openvela-contest-2026-gemini-s1-fw-build-20260829-01
```

执行原则：

1. 以 58 FPS r8 为只读来源，先锁定源文件清单和 SHA-256；不在 r8 原树内运行会写入源码或输出的构建。
2. 在新工作根中物化 r8 自有树以及上述 G0 链接目录；不能保留会把写操作导回 G0 的可写符号链接。
3. 构建缓存单独放在 `/data/codex-caches/openvela-contest-2026-gemini-s1/`，产物和证据单独版本化，不复用旧 Goldfish runtime-output。
4. 主机 policy harness、Goldfish 验证、Gemini-S1 固件构建和实机证据分层记录；主机 PASS 不等于固件集成或实机闭环。
5. 镜像制作继续使用 Gemini-S1 已验证的 `lichee/tools/scripts/pack_img.sh` 与 `lichee/tools/tool/dragon` 流程；没有新授权不得烧录。

## 6. Common policy 主机验证

编译门禁：

```bash
gcc -std=c11 -Wall -Wextra -Werror \
  -Icommon \
  common/mooncat_coach_policy.c \
  tests/host/test_mooncat_coach_policy.c \
  -o mooncat_policy_test
```

`2026-08-29T07:39:41Z` 在 `ubuntu24-hushen` 重放已有二进制：

```text
mooncat_coach_policy: all deterministic cases passed
```

证据哈希：

```text
de0f4f1824c3084b289e9b313e8e1f35a5795a0092fb015cae82229f1e212af5  mooncat_policy_test
ed4248825fc9d927080af05a350bb6144193766749c79238f94880e1f04c7a0e  common/mooncat_coach_policy.c
f7acfe3a85e500e49e3c008025020c705c79aa8d912f9ded258487b4d6fcd4af  common/mooncat_coach_policy.h
948c2b1a37b4573e659aabd44d5bbf608a7db5ce39c2e7a2dd9b91cb482b12b0  tests/host/test_mooncat_coach_policy.c
```

该结果证明确定性 policy 核心可在 Linux host 严格警告门禁下编译并运行；它不证明 `ai_agent`、自定义 Skill、主动任务、QuickApp/velaclaw 或 Gemini-S1 实机已经集成。
