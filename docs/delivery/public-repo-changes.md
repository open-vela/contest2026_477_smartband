# 公共仓改动与复现状态（2026-09-19）

冻结范围为 candidate-08 已验证的主动恢复链路。公共仓修改均已提出配套 PR，等待维护者 review；提出 PR 不等于维护者已接受。正式参赛仓单独由队伍合入。

| 构建树路径 | 官方仓库 | 本作品改动 | 提交/复现状态 |
| --- | --- | --- | --- |
| packages/ai_agent | open-vela/packages_ai_agent | Tool/Skill/通道/本地路由，HTTP framing 和 Skill 分流修复 | [#31](https://github.com/open-vela/packages_ai_agent/pull/31)，Ready |
| packages/demos/openvela_ui | open-vela/packages_demos | Native UI、UI 线程通知、截图和虚拟输入 | [#79](https://github.com/open-vela/packages_demos/pull/79) |
| apps/graphics/lvgl/lvgl | open-vela/apps_graphics_lvgl | LCD partial stride 与显示计数器 | [#45](https://github.com/open-vela/apps_graphics_lvgl/pull/45) |
| vendor/allwinnertech | open-vela/vendor_allwinnertech | Gemini S1 配置和启动集成 | [#22](https://github.com/open-vela/vendor_allwinnertech/pull/22) |

`openvela.xml` 已固定四个 overlay 目标基线：Agent `31faed70f683a6f5e690437c5507891360f0814a`、demos `08b94dd9cdf6e5bf7aad855c6ba43afd75bc5d2d`、LVGL `0f2a49f588505a00e8b46e25a34581c87291a62a`、board `1676386193f0e710121e710935f1757c0f34b662`。Agent 当前上游已有另一版 HTTP 修复，配套 PR 已合并该基线并保留完整 framing 解析；比赛 overlay 继续从固定旧基线应用同一候选修复。其他依赖仍跟随赛事分支。

基线没有 `openvela_ui`；overlay 现在检查 demos 仓存在，在 apply 阶段创建 UI 目录，check 模式不写目标。LCD 副本预先存在的 `tests/Makefile` 删除未纳入提交。

公共 UI PR 的 200 个界面字符串使用八进制 UTF-8 字节表示以满足源码字符检查，与参赛仓基线按 GCC sizeof/memcmp 逐项对比一致；不是翻译或显示内容更换。Agent PR 已整理为当前上游基线上的线性提交，源码树不变。公共 CI 状态以各 PR 实时检查为准；CLA job 与整体状态可能不同，不能把 job 成功当成所有 CI 通过。

已有 candidate-08 历史构建与本日实机证据，Linux 全套主机测试 25 项、实际源码 HTTP 32 项和 Skill 25 项通过。四目标干净归档集成与重复应用也通过，记录见 milestone-12；TFLM 目录复用了现有构建树。完整官方工作区的干净同步重建和全部依赖锁定仍需单独验证，不能由上述结果推断通过。
