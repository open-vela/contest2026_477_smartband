# MoonCat 本地中文工具路由器

这个目录训练一个完全离线、全 int8 的 TFLite Micro 意图模型。模型只做五类
分类：`coach_now`、`coach_preview`、`coach_schedule`、`coach_list` 和
`fallback`。它不生成 JSON；参数检查和 Tool JSON 由端侧确定性代码完成。

输入先按 Unicode code point 生成 2048 维 unigram/bigram 哈希特征；连续 ASCII
或全角数字折叠为同一个数字标记，原始文本仍交给确定性参数解析器。分类器为
`Dense(24, ReLU) → Dense(5, Softmax)`。训练、测试和冻结验收使用不同表达模板族，
并包含天气、音乐、时间、否定请求、工具文档问题和提示注入等 hard negatives。
QuickApp 的真实长提示是单独的产品契约集；训练只组合其稳定协议子句，不包含任何
一条完整契约提示。

训练和导出：

```sh
D:/CodexCaches/mooncat-local-router-venv/Scripts/python.exe \
  local_router/train_router.py
```

输出包括：

- `artifacts/mooncat_intent_router_int8.tflite`：板端模型；
- `artifacts/evaluation.json`：float/int8、门控和 QuickApp golden 结果；
- `artifacts/labels.json`：标签顺序；
- `artifacts/mooncat_intent_router_weights.npz`：可复核训练权重；
- `agent/mooncat_intent_model_data.h`：同一 `.tflite` 的固件 C 数组。

当前固定 seed `20260830` 的主机结果：int8 macro-F1 `0.991879`，gate 后四个正类
最低 precision `0.984375`，测试 fallback 误激活 `0`，冻结验收计划完全匹配率
`0.987179`、fallback 误激活率 `0.003968`，QuickApp 长提示 `30/30`，float/int8
top-1 一致率 `0.998397`。模型为 `52,008 bytes`，SHA-256 是
`8d9fa3a06fd9f0aef417cf92c72fad21a90efe74961077792f82e72ab6eb85d8`。按类指标、
gate 后 confusion matrix、模板隔离计数和全部失败文本都保存在 `evaluation.json`。

训练和主机 TFLite 推理不能证明 Gemini S1 上的延迟、内存、启动或 UI 显示；这些
在真实烧录前保持 `NOT_TESTED`。
