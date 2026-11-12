# RabbitShell 0.1.5 终端光标与复制

0.1.6 沿用本页修复，并新增终端焦点内 Tab／Shift+Tab 前置事件路由，分别发送 09／ESC[Z，保持焦点、允许连续 Tab，不影响普通控件遍历。terminal 已扩为 17 passed；本轮仍未执行真实 shell 补全或 SIGINT 验收，详见 [0.1.6 验证与内存记录](RabbitShell-0.1.6验证与内存记录.md)。以下保留 0.1.5 证据。

2026-11-03，Windows x64、MSVC v143、Qt 6.8.3、libvterm 0.3.3。仅执行独立 offscreen 测试与随机 loopback 端口 fixture；不操作可见桌面、用户窗口、真实服务器或真实凭据。0.1.4 及更早的 dist 目录保留。

## 根因与行为

- `movecursor` 原先只存位置和 visible，没有触发重绘，因此纯左右移动常常要等新文字才看见光标变化。原绘制只画空心框，无闪烁；属性回调只处理 ALTSCREEN。现跟随 libvterm 的位置、CURSORVISIBLE、CURSORBLINK、CURSORSHAPE；默认反色块，DECSCUSR 1/2 块、3/4 下划线、5/6 竖线，偶数常亮；DEC ?25 控制显隐，?12 控制闪烁。属性变化即使位置未变也会重绘。中文宽字符的前导格和占位格共用完整字宽，块内字符反色保留可读性。
- `keyPressEvent` 原先对任意未提前返回的按键清选区。物理 Ctrl+C 先产生 Control KeyPress，已经清掉选区，随后 C 就发送 0x03。现在纯 Control/Shift/Alt/Meta/AltGr 按下不清选区、不发送字节；有有效鼠标拖选或查找选区的 Ctrl+C 复制后清高亮并消费事件，下一次独立 Ctrl+C 才中断。空白选区有效，单击只聚焦并建立拖选起点，Ctrl+C 自动重复一律消费。无选区中断先返回最新输出，有选区复制保持滚动位置。
- 选区由 `selectionActive` 判定，不依赖字符串内容；提取所选字符不再 `trimmed()`，保留选中空格与缩进，支持反向拖选和中文占位格。查找按终端格映射 UTF-16 索引，避免中文前缀和缩进导致高亮错位。复制全部输出仍沿用既有逐行整理方式，本轮没有扩修完整终端文本模型。
- Ctrl+Shift+C 与平台标准 Copy 保持纯复制。macOS 显式物理 Control 使用 Qt 的 Meta 映射，Command+C 作为平台 Copy；本机仅验证 Windows，macOS/Linux 尚待对应系统构建与实测。右键“中断命令（直接发送；无选区时 Ctrl+C）”直接发 0x03。

## 闪烁与资源边界

系统 QStyleHints cursorFlashTime 的半周期用于亮灭，设为 0 或远端 steady 时常亮、不启动计时器。移动、键入、输入法提交和重新聚焦立即恢复亮态并重新计时；输入法光标矩形随远端位置和宽字符更新。只有聚焦、活动、可见且未最小化／未失去暴露、回到当前屏幕底部、远端允许显示时计时。隐藏、失焦、失去活动窗口、最小化、回看历史、远端隐藏时停止并隐藏光标；恢复条件后从亮态开始。监听本控件及所属窗口事件，没有全局轮询。

传入字节引发的光标和文字重绘继续共用已有 16ms 批处理。终端迁移仍在最终可见布局确定后合并 resize。光标位置只由 libvterm 的远端字节反馈决定，左右／Home／End 不本地修改输入缓冲；Workspace 输入路径仍只提交字节。没有新建终端、重启会话或客户端生成提示符。

## 回归与证据

`tests/test_terminal.cpp` 使用 Qt offscreen 私有剪贴板，在 QApplication 创建前固定平台插件，配置与数据置于临时目录。测试显式发送 Control KeyPress → C(Ctrl) → C Release → Control Release；修饰键、按键输入和鼠标选择均作用于测试进程自身控件，不接触用户剪贴板。测试 helper 的物理 Control 键／修饰符按平台选择。

覆盖拖选→首次 Ctrl+C 剪贴板正确且 input=0、清除选区并重绘、自动重复不误中断、第二次独立 Ctrl+C 恰一次 0x03；空白／缩进／反向／中文／占位格、单击未选、查找后的选择、纯复制；纯光标移动触发旧新位置重绘及 IME 矩形、亮灭、hide/show、六种 DECSCUSR 和 ?12、系统闪烁为 0、焦点／隐藏／活动窗口／最小化／历史停启。

复现命令（先在当前进程 PATH 加入 Qt 和 OpenSSL 的 bin）：

```powershell
cmake --build build --config Release --parallel 8
$env:QT_QPA_PLATFORM='offscreen'
$env:RABBITSHELL_TEST_REPORT_DIR=Join-Path $PWD 'test-results/0.1.5'
ctest --test-dir build -C Release -R '^(core|editor_groups|terminal)$' --output-on-failure
$env:PYTHONPATH='.tools/test-python'
& 'C:/Users/JOKER/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' tests/run_integration.py build/Release/rabbitshell_credential_tests.exe loopbackTerminalLayoutDoesNotRestart
```

本轮最终 core 9、editor_groups 23、terminal 16、processes 7，以及活 PTY fixture 3（含 init/cleanup）全部通过，0 failed / 0 skipped。完整测试计数、Release 构建日志、包路径与 exe SHA-256 见仓库 `test-results/0.1.5/delivery-report.md`。两主题截图为真实 Terminal 控件的 libvterm 格渲染，使用确定的 fixture 文本；行中中文占位位置有块光标亮／灭两帧，并另存下划线和竖线。不是可见桌面截图，也不代表真实服务器或操作系统标题栏验收。

| 主题 | 亮／灭证据 | 其他形状 |
| --- | --- | --- |
| Dracula | `terminal-dracula-cursor-on.png` / `terminal-dracula-cursor-off.png` | `terminal-dracula-underline.png` / `terminal-dracula-bar.png` |
| GitHub Light | `terminal-github-light-cursor-on.png` / `terminal-github-light-cursor-off.png` | `terminal-github-light-underline.png` / `terminal-github-light-bar.png` |

所有证据位于 `test-results/0.1.5/`。loopback fixture 只在随机本机端口建立真实 SSH/PTY 通道并 echo，不执行真实 shell；本轮验证移动、分屏、浮窗、收回后原 PTY 连续、没有重复 open-terminal 或 terminalClosed、字节仍到达原通道。Ctrl+C 用 signal spy 验证 input 字节路由，不声称实测远端 shell 的 SIGINT 响应；无选区发送后远端如何响应由远端决定。未重跑无关的整套认证或 SSH/SFTP 功能。
