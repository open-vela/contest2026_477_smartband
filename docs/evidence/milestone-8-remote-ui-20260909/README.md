# Milestone 8：Native UI 截屏与虚拟触摸

为消除反复肉眼观察的操作负担，本次新增现有 SSH 通道上的检查接口。Native UI 的定时器处理截图及虚拟触摸；没有新增网络端口，没有由 Agent/SSH 线程直接调用 LVGL。

实现：`firmware/openvela_ui/openvela_ui_remote.c`；电脑端：`scripts/mooncat_control.py`；命令和边界见 `docs/delivery/remote-ui-inspection.md`。

## 验证与失败记录

- Linux C harness 直接编译实际新源文件，通过带 padding 的 RGB565 截图颜色/长度、tap/swipe 插值与释放、坐标边界和时长检查；模拟 LVGL API 同时断言执行线程。
- Windows 回归：22 passed、3 skipped。3 个跳过项为没有 POSIX C 编译器的旧策略及两个 Native UI harness；新增 UI harness 已另在 Linux 执行，不将 Windows 跳过记为通过。
- 先前 Windows pytest 的共享临时目录访问被拒绝；改用本次独有临时目录后完成回归，未修改共享目录权限。
- candidate-05 ELF、vela.bin、配置及桥接源文件已归档到其打包目录的 `build-reference/`；复用当前构建副本做增量编译，锁定回退及历史 Build04 均未改动。
- 前两次编译虽 exit 0，但新入口未进入最终 ELF。检查发现 `apps/libapps.a` 留存 7 个旧路径 UI 对象；确认全部有新路径对象后移除旧成员并重新链接。失败包未发布、未烧录。
- 移除旧成员后，普通 `ar` 不能正确索引 LTO 对象，导致链接缺失符号；改用工具链的 `gcc-ar` 重建索引。该失败日志保留，不能将失败编译记成通过。

最终链接 exit 0；ELF 已确认存在 `control_tick`、`pointer_read`，二进制包含请求与截图路径，新入口确实进入镜像。全部 16 项静态检查 PASS，见 `audit-gates.txt`。

- 本机镜像：`E:\GeminiFlash\closure-20260908\candidate06.img`
- 远端镜像：`/data/mooncat-closure-pack06/output/Gemini-S1-MoonCat-Remote-UI-candidate-06.img`
- 大小：71,915,520 bytes。
- SHA-256：`1A8DAEF8EC7432E86C92C5405124B6F616DC78432278B3AF54E00957885DF966`，取回本机后核验一致。
- 配置片段显式启用 `CONFIG_LV_USE_SNAPSHOT=y`，两条固件构建脚本均检查该项；相关 5 个配置/构建契约测试通过。

实机截图与虚拟触摸仍须在 candidate-06 烧录后验收；主机 stub 测试不替代实机画面。
