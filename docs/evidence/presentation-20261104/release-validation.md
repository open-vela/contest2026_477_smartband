# 决赛发布检查（2026-11-04）

## 主机回归

环境：本机 WSL ARTEX，Python 3、GCC、Node.js、pytest 7.4.4；使用已有的固定版 packages_ai_agent 测试依赖及 cJSON 源码。

```sh
MOONCAT_CJSON_DIR=<cJSON源码目录> \
MOONCAT_UPSTREAM_DIR=<固定版packages_ai_agent测试源码目录> \
python3 -X utf8 -m pytest -q -ra
```

结果：**54 passed in 27.37s**，0 skipped。

`bash -n` 检查四个公开构建/overlay 入口通过：`apply_overlay.sh`、`apply_local_router_overlay.sh`、`build_gemini_s1_firmware.sh`、`build_gemini_s1_local_router.sh`。本次发布整理沿用 11 月 3 日最终固件与板端验收记录。

## 材料检查

- 23 页 PPT 在原生 PowerPoint 中逐页渲染检查；包结构与布局检查为 0 findings，并通过再次导入检查。
- 定稿文件 104,551,229 字节，作为 Release 附件上传；仓库新增 9 张 320×240 实机 PNG。
- 新增/变化文本的常见凭据模式扫描无命中；提交文件保留公开源码、测试和整理后的证据。
- 完整设备日志、设备数据备份和应用原始状态文件保留本机。
