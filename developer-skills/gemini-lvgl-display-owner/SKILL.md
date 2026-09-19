---
name: gemini-lvgl-display-owner
description: Diagnose Gemini S1 openvela white screens or SPI conflicts when Native LVGL UI and QuickApp/VAPP may both own the LCD; verify the selected display path with device evidence.
---

# Gemini S1 单一显示所有权定位

输入：运行固件版本、当前进程列表、启动配置、故障发生前的操作和现场屏幕观察。适用于本项目的 Gemini S1 LCD/SPI1 路径；不要把此板结论直接套到其他显示驱动。

1. 通过已验证的 SSH 或短 ADB 命令读取 `uname -a`、`ps`。确认 `openvela_ui`、`vapp`、`luncher_mini` 是否并存。进程存在只能证明启动，不能证明其持有 LCD 或画面正常。
2. 对照最终 `.config` 和板级 `rcS`，核查哪个入口启动显示。配置片段不等于最终构建配置；检查 `CONFIG_BASE_DEFCONFIG` 是否指向所选基线。
3. 结合 LCD 打开路径和 SPI/LCD 错误日志定位双显示入口。保留错误样本，不把累计启动错误计数当作当前采样窗口新增错误。
4. 在当前授权内选择一个显示所有者。月薪喵主动演示选择 Native `openvela_ui`；通过 CLI 设置任务。QuickApp 单独展示，未经设备验证不承诺同时弹原生卡。停止应用或普通重启前记录任务状态。
5. 重新读取进程，现场观察画面、触摸、提醒卡；分别记录 PASS、FAIL、NOT_TESTED。页面 `Page ready` 或 `Page show` 不替代实体点击；UI 日志不替代屏幕观察。
6. 如测量性能，分别给出 motion、visual、render、flush 及采样时长、错误增量。检查 PARTIAL 绘制使用实际 stride。不得把历史固件约 58 FPS 写成当前固件结果。

判断标准：单一显示路径已确认、预期画面可见、操作可重复、采样窗口无新增 LCD/SPI 错误。若无法观察实物，保留屏幕项 NOT_TESTED；不要为完成报告补写成功。

已有案例：2026-08-29 排除 `luncher_mini` 与 Native UI 的竞争后，短包名 `com.ov.mooncat` 可加载 QuickApp。VAPP 与 Native UI 并发显示仍不是已验收能力。对应证据见 [白屏修复记录](../../docs/evidence/milestone-4-white-screen-fix-image-20260829-03/)。

输出：固件标识、所选显示入口、故障原因与最小改动、设备命令证据、屏幕/触摸结果、当前未验证项。刷写涉及项目 `AGENTS.md` 的镜像与升级模式授权，不能从本 Skill 推导烧录许可。
