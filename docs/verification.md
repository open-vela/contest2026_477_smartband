# 本地与构建树验证

## 1. 仓内静态和主机测试

```sh
python -X utf8 -m pytest -q
bash -n scripts/apply_overlay.sh
python -X utf8 -m py_compile scripts/merge_defconfig.py
```

这些测试验证 r7/r8 哈希、不回退的 sparse/stride 关键标记、60 Hz 节拍、赛事配置
合并的幂等性，以及主动教练的主机策略；它们不等价于 Gemini S1 编译或实机验证。

## 2. 干净 openVela 树

把官方 `dev-ai-contest-2026` manifest 同步到新的版本化目录，禁止对历史 r8 证据树
执行 overlay：

```sh
./scripts/apply_overlay.sh --check /path/to/clean-openvela
./scripts/apply_overlay.sh --apply /path/to/clean-openvela
./scripts/apply_overlay.sh --verify /path/to/clean-openvela
```

脚本只覆盖显式 allowlist 内的 UI、LCD、ai_agent Tool、Skill 和 board defconfig，
并对 `packages_ai_agent_overlay.patch` 先做 `git apply --check`。它不会清理目标树，
不会复制 QuickApp 到一个猜测路径，也不会构建、打包或烧录。

## 3. 构建验收

在干净树按官方 Gemini S1 方法配置并编译。至少保存：manifest revision、完整配置、
编译命令/退出码、`vela.bin` SHA-256、编译日志和未跟踪状态。先验证 ai_agent、
velaclaw、产品 UI 与自定义 Tool 都进入产物，再执行 IMAGEWTY 打包。

候选镜像必须使用新文件名，并与锁定镜像进行 22 条目容器审计、`Vnsh/Vres/Vusrdata`
边界/长度/哈希核对和 payload 差异解释。生成候选不代表授权烧录。

## 4. 实机边界

只有用户再次明确授权后才能烧录。板端验收需分别记录启动、显示、Wi-Fi、主动任务、
Tool 审计、UI 提醒，以及 motion/visual/render/flush FPS、missed ticks、decode errors、
LCD ioctl errors。未完成此步骤前，结论只能是 host/static/build validated，不能称为
新的实机 58 FPS AI 固件。
