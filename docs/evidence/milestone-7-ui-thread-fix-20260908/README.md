# Milestone 7：主动提醒 UI 线程修复（待上板复验）

candidate-04 实机通过初始启动、本地定时路由和重复请求去重；两轮到期附近发生屏幕卡死和网络失联。第二轮通过 WebSocket 操作开机 Agent，未重启 Agent，因此排除了首轮 CLI 重启的干扰。

Agent outbound 回调原来调用 `lv_async_call`，该函数会操作 LVGL 内存和定时器，而 Build04 的实际配置为 `CONFIG_LV_USE_OS=0`。本次唯一功能改动是回调只发布受互斥保护的消息，由 Native UI 自己创建的 100 ms 定时器消费。开始/停止和全部 LVGL 对象操作均留在 UI 线程。

## 验证

- 原有 Windows 主机回归：22 passed、1 skipped。
- 新增实际 C/pthread 桥接测试：Linux `/usr/bin/cc` 编译和执行 PASS；将同一测试用于旧桥接实现，因跨线程 LVGL 调用断言触发 SIGABRT，预期 FAIL。测试文件为 `tests/test_bridge_thread.py`。
- 隔离目录 `/data/mooncat-closure-build05` 构建 exit 0；共享只读工具链，未改动 Build04。新 ELF 存在 `mooncat_show_pending`、`g_pending_timer`，旧 `mooncat_show_pending_async` 不存在。
- 打包首轮发现历史 staging 的资源内容不等于正式 candidate-04，已保留该失败包且未交付。最终使用正式 candidate-04 已审计提取内容，仅替换 `nsh.fex`、`Vnsh.fex`，调用原官方 dragon 打包。
- 最终镜像 16/16 静态门槛 PASS，22 项容器、边界和校验通过。日志见本目录。静态通过不证明卡死已修复。

## 候选

- 本机：`E:\GeminiFlash\closure-20260908\candidate05.img`
- 远端：`/data/mooncat-closure-pack05-final/output/Gemini-S1-MoonCat-UI-Thread-Fix-candidate-05.img`
- 大小：71,915,520 bytes。
- SHA-256：`B5912708C4B8CA34A9B110B8F9A00C254AA6D71F98304BD10F05AD52FE98969C`，本机取回后核验一致。
- 实机复验：NOT_TESTED，需新镜像烧录授权。下一轮保持开机 Agent，通过 WebSocket 发送 UTF-8 请求，确认到期屏幕提醒和系统持续响应。

比赛交付仍未关闭，不合入专属仓最终交付版本，不预写成功视频旁白。
