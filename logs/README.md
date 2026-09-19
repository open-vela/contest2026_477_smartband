# logs/ — AI Coding 日志目录

存放你在开发中与 AI 工具的对话日志，和作品代码一并提交。

本仓的真实记录位于 `QinXi-ai/`。2026-09-19 官方校验器通过 2 个文件、1588 条事件。原有 1358 条记录保留；新增 230 条来自明确选定的 2026-09-09 Codex 会话。

原生 Codex JSONL 先由选择适配器提取用户/助手文本及真实工具调用（保留原 call_id），再交给官方 snapshot_core.py v1.3.0 的 append_events、自动脱敏与 manifest writer。适配器不导出系统/开发者消息、推理、工具输出或媒体；不手工改写生成后的事件。除官方规则，还自动清除私钥、用户路径、私有 IP、主机别名和邮箱。此为选定会话的开发过程记录，不代表全部开发活动；不以日志缺失部分推断未开展工作。

## 目录结构

```text
logs/
└── <github_login>/              # 你的 GitHub 用户名，一人一目录
    ├── manifest.json            # 会话清单
    └── <date>/                  # 日期 YYYY-MM-DD
        └── <tool>__<sid>.jsonl  # 一个会话一个文件（工具名与 session id 用 __ 连接）
```

- `<tool>`：`claude-code` / `opencode` / `codex` / `kiro`
- 每个 `.jsonl` 每行一个事件，由组委会提供的日志归集工具导出，**只提交 JSONL 本身**。

导出与提交的完整步骤、字段定义见[《AI Coding 日志归集与提交手册》](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_coding_log_guide.md)。
