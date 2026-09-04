# Milestone 1：一键主动恢复教练闭环

日期：2026-08-29

## 已实现

现场按钮触发同一条链路：

`QuickApp → @system.velaclaw → 自定义 Skill → mooncat_coach_tick → mooncat channel → native UI`

固定的 75 分钟久坐模拟输入会命中端侧确定性策略。Tool 在 `execute` 模式推送
8 秒 native UI 提醒，QuickApp 同时显示 `res.reply`；冷却期、勿扰、运动中、低电量
和不同恢复动作均由 C policy 处理。

## 构建结果

- hushen 独立副本：`/data/openvela-contest-2026-gemini-s1-v1-build-20260829-02`
- `make -j16`：exit `0`
- 合并后 `.config` SHA-256：
  `58b03e1651f701472f76f17b36a8b6c76cf7d2091de5ea82ee251022a31e0dea`
- `vela.bin`：`4,639,888 bytes`
- `vela.bin` SHA-256：
  `b249b331f1fff3ba7ae9dec85c386af8bd620ca3bd475a933148ce8a204f885d`
- `nuttx.elf` 含 `tool_mooncat_coach_execute`；二进制含 `mooncat_coach_tick`、
  `mooncat-active-coach`、`MOONCAT ACTIVE COACH` 和 bridge 日志字符串。

本地副本位于 `output/Gemini-S1-MoonCat-AI-contest-20260829.vela.bin`。

QuickApp 使用 `aiot-toolkit 2.0.5` 构建通过；调试 RPK 为 `13,882 bytes`，
SHA-256 为 `faf86d4b5e42e846bdff78de411c291c63b231081ac4affb3200185f7fa35eab`。

## 测试

- `python -X utf8 -m pytest -q`：`17 passed, 1 skipped`
- `npm run verify`：PASS
- `git diff --check`：PASS

当前完成的是源码、QuickApp 包和 Gemini S1 固件编译闭环；没有制作新 IMG，也没有
烧录设备。因此约 58 FPS 是锁定产品基线的实机成绩，新 AI 固件的现场运行与 FPS
将在获得单独烧录授权后验证。
