# RabbitShell 0.1.5 进程刷新与卡片验证

0.1.6 保留本页刷新与卡片规则，移除进程表上方标题和下方可见说明，CPU／RSS 含义仅在表头 tooltip 展示；processes 仍为 7 passed。新纯卡片截图与其他结果见 [0.1.6 验证与内存记录](RabbitShell-0.1.6验证与内存记录.md)，下文为 0.1.5 历史证据。

2026-11-03，和终端修复合入同一 0.1.5，未覆盖任何旧 dist。

## 行为与边界

`Workspace::monitor()` 原先每轮对 `processes` 与 `miniProcesses` 执行 `setRowCount(0)`，导致选择、current 和现有 item 随数据重建丢失。现在两处使用共享 `ProcessTable`，由已有采样解析成 `ProcessInfo` 快照，按 `ProcessIdRole = Qt::UserRole + 1` 跟踪 PID。`Qt::UserRole` 单独存数值排序值（含 RSS 原始字节），身份和排序不混用。

批量更新前保存选中 PID 集合、current PID／列、排序列／方向、横纵滚动；暂停重绘与排序，只删已离开样本的行，仅为新 PID 创建 item，其余更新已有 item。结束恢复排序，再按 PID 恢复选择与 current，通过 selection model 设置 current 避免自动滚入视图。滚动恢复至原像素值，数据减少导致范围缩小时由 Qt 限制到合法值。不重新分配用户数值列／用户列宽，不变更焦点。

完整表沿用监控样本范围，侧栏只显示采样顺序的前五项并可在这五项内排序；离开前五只取消该可见选择，即使 PID 仍出现在完整样本中。消失 PID 不转选占用原行号的新 PID。当前身份只按 PID 与相邻快照判断；没有增加 starttime、远端命令或 SSH 协议字段，不能严格防止两个快照间快速退出／复用 PID 被视为同一项。

## 外观

两张表统一整体 8px 圆角、1px 外框，表头独立主题底色且只在大卡顶部形成圆角，下边直，与正文连续。无行间线、竖网格、表头底线或表头与 body 间隙；交替行底色区分条目，选中背景覆盖交替底色，失焦仍保持。正文 13px，数字右对齐；完整表名称伸展、状态紧凑，其他列可调宽，窄视图在内部滚动。侧栏保留紧凑布局，没有新工具栏／标签／按钮。

颜色集中在 `Colors` 的 processHeader／processBody／processAlternate／processSelection，外框与文本沿用 cardBorder／text。局部 semantic property 仅作用于进程表。轻量 header 绘制排序箭头，外卡绘制圆角，内表整体裁角，只有 1px 留边，无图形特效、动画或全局轮询。

## 验证

永久回归 `tests/test_processes.cpp` 通过真实离线 Workspace 的 `monitor()` 路径连续提交快照，检查 CPU／RSS 排序下的 PID 跨行重排、数值更新、item 指针复用、多选与 current／列、排序方向、横纵滚动、列宽与焦点保留，以及选中项退出、新 PID 替代、空快照。侧栏同路径覆盖 top5 重排与离开前五，不连接网络。

测试在 QApplication 创建前固定 `QT_QPA_PLATFORM=offscreen`，使用独立临时配置与数据。两主题截图来自真实生产控件：完整表包含足够行数和选中态，侧栏宽度 290px，保留排序箭头及失焦选中态。没有使用可见桌面、真实服务器、真实凭据或运行中用户窗口。Windows x64／Qt 6.8.3 已执行；macOS/Linux 尚未构建与实测。

相关命令：

```powershell
cmake --build build --config Release --parallel 8
$env:RABBITSHELL_TEST_REPORT_DIR=Join-Path $PWD 'test-results/0.1.5'
$env:QT_QPA_PLATFORM='offscreen'
ctest --test-dir build -C Release -R '^(core|editor_groups|terminal|processes)$' --output-on-failure
```

最终 Release 构建成功，core 9、editor_groups 23、terminal 16、processes 7，均 0 failed / 0 skipped；既有活 PTY fixture 3（含 init/cleanup）通过。主代理实际审查四张进程截图，已确认两主题完整表／mini 均符合最终外观规则。截图原由 CTest 在 `build/test-results/0.1.5/` 生成，已归档到仓库 `test-results/0.1.5/`；以上命令使用绝对报告目录避免 CTest 工作目录歧义。最终包路径及哈希见仓库该目录 `delivery-report.md`。

| 主题 | 完整表 | 窄侧栏 |
| --- | --- | --- |
| Dracula | `process-dracula-full.png` | `process-dracula-mini.png` |
| GitHub Light | `process-github-light-full.png` | `process-github-light-mini.png` |

合并版继续运行 core、editor_groups、terminal、processes 以及既有 `loopbackTerminalLayoutDoesNotRestart`，不扩散整套无关认证测试。终端 fixture 仅验证 PTY 连续和字节路由，不证明真实 shell SIGINT 响应。终端修复与两主题光标截图保留，详见 [终端验证](RabbitShell-终端光标与复制验证.md)。
