# 提交前验证（2026-09-19）

## 已完成

- Ubuntu 24.04：完整仓内 `python -X utf8 -m pytest -q`，25 passed / 0 skipped，见 `pytest-linux.txt`。
- 四个固定公共仓基线的全新归档目录：主 overlay、本地路由 overlay 各执行 check → apply → apply → verify，全部通过，见 `closure-integration.txt`。check 前 UI 目录不存在。
- 在上述实际打补丁源码上编译运行 HTTP 接收 harness：32 项通过；Skill 分流 harness：25 项通过。
- 四个 shell 入口 `bash -n` 通过。修复了文件无执行位时脚本内部直接调用导致的 Permission denied，入口和内部调用均显式使用 Bash。
- 添加 `pytest.ini` 将默认收集范围设为 `tests/`，避免把需要外部源码参数的 CLI harness 当成 pytest 模块导入；按 README 默认命令重跑为 25 passed。
- 官方日志校验：2 个文件、1588 条事件 ALL OK，见 `log-validation.txt`。日志残留模式扫描为 0，见 `log-residual-scan.json`；模式扫描不构成穷尽式隐私保证。

## 复跑环境

Python 3.12、pytest 9.1.1、numpy 2.2.6、Node.js 22.14.0、GCC。主机测试使用任务虚拟环境；Python 和 Node 依赖在联网主机下载后转移至构建机。

```sh
python -m pip install pytest numpy
python -X utf8 -m pytest -q
python scripts/test_http_chunked_reader.py --source ../packages/ai_agent/src/infra/vela_tls.c --output-dir /tmp/mooncat-http-tests
python scripts/test_skill_context_route.py --source ../packages/ai_agent/src/core/agent_loop.c --output-dir /tmp/mooncat-skill-tests
```

## 验证范围

四目标测试从官方 Git 固定版本归档重新展开，具体版本见交付目录 `public-repo-changes.md`。TFLM 路径链接至既有构建树，仅用于满足目录布局检查；未重新同步或编译该依赖。这不是完整 `repo sync` 后的全量固件构建。

candidate-08 已有历史构建和本日真实设备核心功能证据（milestone-10、11）。本次构建机 GitHub DNS 不可用、数据盘仅约 1.9 GB 空间，旧构建树的 Git 元数据不完整；未覆盖、清理旧基线来制造全新重编结论。除四个目标外，manifest 仍引用赛事分支，完整依赖冻结及从零同步重编保持 NOT_TESTED。此限制影响复现把握，不把它列为官方明确要求的额外提交入口。

公共 UI PR 为满足源码字符检查，将 200 个界面字符串改成固定三位八进制 UTF-8 字节表示。该表示调整仅在公共 PR，参赛仓保留已验证显示基线；以 GCC 编译的 harness 对全部 200 对字符串检查 sizeof 与 memcmp，全部一致。Agent PR 改为最新上游基线上的线性提交，树内容与原已测试提交完全相同，避免公共 CI 无法 cherry-pick 合并提交。
