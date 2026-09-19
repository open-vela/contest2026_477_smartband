# 板端截屏与虚拟触摸

candidate-06 新增 Native UI 检查入口。通过现有 SSH 文件通道发送请求，UI 线程的 20 ms 定时器执行命令并返回请求 ID、结果码和 UI tick。不新增网络监听端口，不从 Agent 或 SSH 线程直接调用 LVGL。

## 电脑端命令

先恢复板子网络和已核验主机指纹的 SSH 公钥连接。IP 由 DHCP 分配，使用实际地址。

```sh
python scripts/mooncat_control.py --host 172.20.10.8 screenshot work/screen.png
python scripts/mooncat_control.py --host 172.20.10.8 tap 160 120
python scripts/mooncat_control.py --host 172.20.10.8 swipe 250 120 70 120 --duration 400
```

可以通过 `--key` 和 `--known-hosts` 指定自己的客户端身份及已核验主机密钥文件。脚本不跳过主机验证。截图转换需要 Pillow；板端输出 PPM，电脑校验完整数据长度后转换为 PNG。

## 语义与范围

- `screenshot`：对当前活动屏幕及其子对象调用 `lv_snapshot_take`，使用真实返回 stride 解码 RGB565。先写临时文件，再原子替换 `/tmp/mooncat-screen.ppm`。MoonCat 提醒卡属于活动屏幕的子对象，包含在截图中；不承诺抓到 LVGL 独立 top/system layer。
- `tap`：在有效屏幕坐标按下 100 ms 后释放。
- `swipe`：在 20–2000 ms 内逐步移动并释放；坐标越界或零时长返回错误。
- 虚拟触摸使用独立 LVGL pointer input，设为事件模式，由 UI 定时器驱动；不修改真实触摸驱动。
- 只允许一个控制客户端串行发命令。手动触摸与自动触摸不要同时进行。运行中的手势对新命令返回 `EBUSY`。
- 截图会额外渲染一次并写入 tmpfs；建议先按约 1 秒间隔采样。截图期间的性能不能用于证明正常运行 FPS。
- 截图验证板端渲染内容，不替代实体 LCD/背光/花屏检查，也不属于实体手指触摸证据。

## 传输协议

请求文本格式为 `ID screenshot`、`ID tap X Y` 或 `ID swipe X0 Y0 X1 Y1 DURATION_MS`。客户端先写 `/tmp/mooncat-control.req.part`，再重命名为 `/tmp/mooncat-control.req`。UI 取走请求后写 `/tmp/mooncat-control.status`，内容为 `ID RESULT UI_TICK`。客户端只接受匹配 ID 的回应，避免使用旧截图确认新命令。临时文件在重启后消失。

## 当前验证

Linux C harness 已直接编译并运行实际板端源文件：带 padding 的 RGB565 红绿蓝白截图逐字节符合预期；tap/swipe 按下、插值、释放以及错误输入检查通过。全部模拟 LVGL API 均检查调用线程。

该验证使用主机 LVGL stub，不能代替 candidate-06 实机截图和虚拟触摸验收。烧录后先抓取主页，再抓提醒前后画面；保存命令回执、PNG 和设备版本。未收到回应或传输不完整时记录失败，不写成功结论。

### 2026-09-09 实机补充

candidate-06 已读取 `Sep 9 2026 12:05:31` 编译时间并成功调用新增接口。主页截图、跨页滑动、城市选择点击均 PASS。独立冷启动后的定时演示取得 32/32 完整序列截图：提醒卡连续出现在 3 帧中，随后自动消失；猫动画、到期后的虚拟滑动和 SSH 均继续工作；重复建任务被拒绝，到期后列表为空。一轮渲染提醒闭环 PASS。

完整证据见 [实机记录](../evidence/milestone-9-live-remote-ui-20260909/README.md)。此前首轮因电脑端 Python 依赖混用漏采，不计为视觉闭环通过；第二轮前存在一次用户 USB 冷重启。验收重点为多样功能的真实链路覆盖，不以多轮重复计数作为门槛；截图/虚拟触摸的证据边界不变。
