# RabbitShell 0.1.8 验证记录

2026-11-04。Qt 6.8.3 Widgets / C++20 / MSVC，Windows x64 Release。仅 offscreen、临时配置、随机 loopback SSH/SFTP fixture；未操作可见桌面、用户配置、真实凭据或服务器，无 commit/reset。旧 dist 保留。

## 本版行为

- 总栏显示当前终端图标／省略名称／完整 tooltip，管理选中行复用拆分与垃圾桶按钮，按下时绑定行 ID。鼠标／键盘／终端 focus、新建、拆分、重命名、移组和关闭同步；键盘不抢列表焦点，组标题不清除活动项。当前关闭优先同组相邻，非活动关闭保持其他焦点；仅一终端隐藏管理栏，零终端清空并禁用相关操作。
- 单终端直接关闭，无论忙闲均不确认、不探测。启动／关闭 probe、tokens、terminalprocess 模块和专用测试已移除。整工作区／应用退出及编辑未保存保护保留。内部名称行和更改颜色移除，关闭入口统一 Trash，PanelMaximize／PanelRestore 使用独立四角图标；最大化只影响右侧并恢复原布局／高度。
- 移除终端进入编辑区的菜单和泛型入口，EditorArea 也拒绝 terminalPage。独立终端浮窗返回原组，保持同一 PTY／ID／Terminal；编辑文件分屏／浮窗不变。重命名同步管理、总栏和浮窗标题。
- Delete 只归真实文件树焦点，行内输入、路径、筛选、文件编辑器和终端不触发删除；无选择无确认、忽略重复。右键与 Delete 共用默认取消确认、路径值快照、父子去重与局部刷新；确认返回后核对在线和同一 QPointer<Session>。删除资源管理底部提示块。
- 去掉路径输入按钮；框内空白左击切编辑并全选当前路径，目录段直接导航。Enter 显式提交，Esc／失焦恢复已提交路径；无新增窗口级 Ctrl+L。
- 每次传输尝试由 worker 累加成功 SFTP read／write 的 payload，目录跨文件累计、跳过不计，失败已传字节不冒充提交成功。单调时间包含枚举和原子提交，排除排队与 worker 显式暂停等待；120ms 最近单区间速度、暂停重置采样、终态强制上报，短任务不依赖节流。平均按 bytes / activeMs（分母至少 1ms），重试归零；零字节／全部跳过显示 0 B/s，部分失败标本次已传平均，失败／取消明确状态。任务行保留当前文件进度和速度，总速率与方向同步且终态清除；无新轮询或样本缓存，历史 trim 一并回收 item 中统计。

## 自动回归

报告目录：`D:/UserFiles/Documents/ChatGPT/RabbitShell/test-results/0.1.8`。数量含 init/cleanup。

| 套件 | passed | failed / skipped | 日志 |
| --- | ---: | --- | --- |
| core | 9 | 0 / 0 | core-Release.txt |
| editor_groups | 29 | 0 / 0 | editor_groups-Release.txt |
| terminal | 17 | 0 / 0 | terminal-Release.txt |
| processes | 8 | 0 / 0 | processes-Release.txt |
| controls | 6 | 0 / 0 | controls-Release.txt |
| workspace | 14 | 0 / 0 | workspace-Release.txt |
| SSH/SFTP fixture | 3 | 0 / 0 | rabbitshell_session_tests.txt |
| 存活 PTY／布局／未保存保护 fixture | 5 | 0 / 0 | rabbitshell_credential_tests.txt |

相关 CTest 6/6 通过，日志 ctest-final.txt。最后一项整行选择状态小改动后，额外仅复验 workspace（ctest-workspace.txt）。删除测试保留确认后真实提交断言，确认期间断线／更换 Session 两种情况均拒绝；文本 Delete 保留原义。终端测试覆盖焦点反向保选、连续键盘切行、组标题、pressed 后 focus 改变、重命名、同组邻居、非活动关闭、空终端及失焦／最大化后 selected/current。原 PID／网卡偏好、行内新建、编辑布局、Tab／Ctrl+C 回归继续通过。

SSH/SFTP 覆盖真实字节、递归短文件累计、零字节、全部跳过、部分失败、暂停排除、实时区间、取消、同 ID 从头重试及终态回报；fixture 只对测试名 meter-slow 文件延时。PTY fixture 验证移入编辑区被拒绝、浮出／收回同一通道、不重开 shell，关闭一个 PTY 后另一个继续 echo。exec 审计无旧关闭 probe 请求。该 fixture 不执行真实系统 shell，不能据此声称真实 shell 补全、SIGINT 或 OpenSSH 服务器已验收。

## 两主题实际控件截图

`workspace-dark-*`、`workspace-light-*` 均由生产 QWidget::grab 生成：`inline.png`、`terminal.png`、`path-edit.png`、`transfer.png`、`transfer-complete.png`、`maximized.png`、`restored.png`。终端图涵盖当前名／选中行操作／无内部标题；完成任务图由合法任务 ID 通过生产 completed handler 生成，批次为 2 结束／0 等待，总栏无残留实时速度。传输界面截图采用受控任务信号；实际 payload／暂停／重试由上面的真实协议 fixture 另外验证。

失焦及最大化后 selected/current 属性与行操作保持，局部失焦选择样式已显式定义；Windows offscreen 最大化截图仍可能呈现不同的选中底色。未以此代替原生桌面激活／合成效果验收。未复做内存压力、真实 DPI 多屏、macOS/Linux 客户端或无 SDK 干净系统验收。原生窗口验证与 offscreen 范围分开。

## 复现与交付

```powershell
$env:PATH="$PWD/.tools/Qt/6.8.3/msvc2022_64/bin;$PWD/.tools/Qt/Tools/OpenSSLv3/Win_x64/bin;$env:PATH"
$env:QT_QPA_PLATFORM='offscreen'
$env:PYTHONPATH="$PWD/.tools/test-python"
$env:RABBITSHELL_TEST_REPORT_DIR='D:/UserFiles/Documents/ChatGPT/RabbitShell/test-results/0.1.8'
cmake --build build --config Release --parallel 8
ctest --test-dir build -C Release -R '^(core|editor_groups|terminal|processes|controls|workspace)$' --output-on-failure
& 'C:/Users/JOKER/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' tests/run_integration.py build/Release/rabbitshell_session_tests.exe
& 'C:/Users/JOKER/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' tests/run_integration.py build/Release/rabbitshell_credential_tests.exe loopbackTerminalLayoutDoesNotRestart loopbackDirectClosePreservesOtherPty loopbackUnsavedReopenAndKeyCorrection
./scripts/package-windows.ps1 -Destination ./dist/RabbitShell-0.1.8
```

新包：`dist/RabbitShell-0.1.8`，附本验证文档。最终清单／包烟测与哈希见报告目录 package-manifest.json、package-verification.json、delivery-report.md。

0.1.8 EXE SHA-256：`724567061FEDB853B7C7795064D130F6A7D670B3F43B1C40DD62125C4B2C7325`。

旧 0.1.7 EXE SHA-256 保持：`6577BC5572DC71DDABD9704889DBDBD985701409DAB39C1CCAD21EAC40217ABD`。旧验证记录保留当时内容，其关闭检测和终端进入编辑区规则已被本版覆盖；旧 probe 命令不再适用于当前源码。
