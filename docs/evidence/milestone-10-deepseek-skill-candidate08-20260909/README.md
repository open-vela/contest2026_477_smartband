# DeepSeek HTTP 与 Skill 路由候选：candidate-08

状态：源码/主机/构建/镜像静态验证 PASS；candidate-08 实机 **NOT_TESTED**，尚未烧录。

candidate-06 的 DeepSeek HTTP/1.1 chunked keep-alive 响应已收齐，但接收循环继续等待 EOF/读超时，超过 Agent 60 秒 watchdog。另一个独立问题是结构化 Skill 请求中的 `battery_percent` 被旧关键词 `battery` 抢先匹配，实际调用了查电量工具。

本候选包含 `packages_ai_agent_http_chunked.patch` 和 `packages_ai_agent_skill_route.patch`；后者让明确 Skill／观察字段输入进入完整 Agent 路径，保留简单技能发现和普通本地命令。两项均由 `scripts/apply_overlay.sh` 安装。

| 验证 | 实际结果 |
| --- | --- |
| TLS 实际 C 接收函数 | 旧版复现多一次读取；新版 32 项 PASS，ASan/UBSan |
| NL 实际 C 分流函数 | 旧版复现误查电量；新版 25 项 PASS，ASan/UBSan |
| Windows 仓库测试 | 22 passed、3 skipped；跳过不计通过 |
| 固件构建/最终链接 | PASS |
| 最终 ELF | 已查验内联 guard 的 marker 表和早退分支；归档只有一个当前 agent_loop 成员 |
| 镜像静态核验 | 16/16 PASS；参考/回退 payload 差异仅 nsh/Vnsh |
| 本机候选传输 | 长度和完整 SHA-256 与已审计远端镜像一致 |
| 新候选板端对话、真实 Skill 使用、运动、天气 | NOT_TESTED；旧板端失败项没有关闭 |

入口为 `LOCAL-CANDIDATE.txt` 和 `audit-gates.txt`。`skill-test-result.txt`、`http-test-result.txt` 保留旧版失败对照；`handle-nl-final-disassembly.txt`、`skill-final-elf-audit.txt` 是最终 ELF 证据。不会由技能列表推断 LLM 已读取并执行 Skill，也不会由编译成功推断硬件通过。

运动源码本次未修改，尚无已证实根因。没有提交/推送、烧录或写 boot0；私有配置不包含在此证据包中。
