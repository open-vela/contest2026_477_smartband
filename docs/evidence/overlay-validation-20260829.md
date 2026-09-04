# 58 FPS overlay 主机验证记录

时间：2026-08-29（Asia/Shanghai）

## 基线身份

- 本地锁定镜像：`68,699,136 bytes`；SHA-256
  `F42AC571713FD35FD04722DE01DFC3E60AF96D4C80DAA8709E81A214436C2BBC`；
- hushen r8 构建树中的最终镜像：同一 SHA-256；
- hushen `nuttx/vela.bin`：
  `92FD8EF173E9661CA3430083533C39A97C1FBFBA6821960A18E14853C86670C1`；
- r7 UI、r6 main、r8 LCD 和产品 defconfig 在本仓、Windows 原始文件与 hushen
  最终树三方哈希一致。

## 静态/主机测试

```text
python -X utf8 -m pytest -q
14 passed, 1 skipped in 0.22s

bash -n scripts/apply_overlay.sh
PASS

python -X utf8 -m py_compile scripts/merge_defconfig.py
PASS

git diff --check
PASS（仅 Git 的 Windows LF/CRLF 提示，无 whitespace error）
```

## overlay smoke

在 `.test-tmp/overlay.XXXXXX` 创建一次性目标树：

- `packages_ai_agent` 从官方 `dev-ai-contest-2026` 分支浅克隆；
- UI、LVGL 和 Gemini board 只创建脚本所需布局；
- 依次运行 `--check`、`--apply`、`--verify`、再次 `--apply`；
- 两次应用后的赛事 defconfig SHA-256 均为
  `13BD8B37F526B544723A9B930027980488C2CEC9D2DF2009384D62775B02D1E9`；
- `/data/agent`、MQ 4096、Skill 留档、ai_agent CMake/registry patch 均通过验证；
- 结果：`PASS (apply + verify + idempotent reapply)`。

一次性目标树在验证后按精确路径删除；未改动历史 r8 构建树。

## 未完成/禁止外推

本记录没有执行完整 openVela 编译、IMAGEWTY 打包或实机烧录。它证明源码身份、配置
合并和 overlay 契约，不证明新增 AI 版本已在 Gemini S1 上启动或仍达到 58 FPS。
