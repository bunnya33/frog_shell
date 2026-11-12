# RabbitShell 0.1.6 验证与内存记录

2026-11-03。Windows 11 x64、MSVC、Qt 6.8.3、OpenSSL 3。报告统一位于绝对目录 `D:/UserFiles/Documents/ChatGPT/RabbitShell/test-results/0.1.6`。本轮只运行生产控件的 offscreen 测试与独立 loopback fixture，使用临时数据、配置及测试进程私有剪贴板；未操作可见桌面、真实服务器、真实凭据或用户运行中的窗口。

## 本轮实现

| 项目 | 行为与实现边界 |
| --- | --- |
| 进程页 | 删除卡片上方“进程信息”与下方 CPU 说明，移除 processDetail；CPU／RSS 含义仅在表头 tooltip 展示。保留 0.1.5 PID 增量刷新、选择、排序、滚动及整体圆角卡片。 |
| 编辑组同行工具 | 删除 editorHead；每组页签右侧为默认向右分屏按钮（可见箭头菜单向右／向下）及合并按钮。图标 20px，点击区分别 42×30／30×30px，垂直居中。动作绑定所属组，全部合并归点击组。菜单生存期保护、工具区拒绝拖放、长页签滚动和单页禁分屏均保留。terminalHead 不变。 |
| 统一下拉框 | 网卡、终端方向、认证、环境使用 StyledComboBox，保留原生 Qt model/view、popup、键盘、无障碍和信号。主题边框／圆角／内边距、当前项勾选；下方 4px 优先，不足向上，屏幕限宽限高并滚动，不覆盖自身。Esc 不提交，重开恢复已提交项；父窗口移动关闭。无搜索、多选、特效或轮询。 |
| 终端 Tab | 根因是 QWidget 焦点遍历先于 keyPressEvent；Terminal::event 对聚焦终端内 Tab／Backtab 前置路由到 libvterm。Tab 恰发 09，Shift+Tab 恰发 ESC[Z，焦点保留且允许连续／自动重复；普通控件仍正常遍历，其他修饰组合不扩大拦截。补全由远端完成。 |
| 按钮资源保留 | Qt setTabButton 只隐藏旧控件。EditorTabs 及 MainWindow 的替换／移除路径通过共享 replaceTabButton 对旧控件 deleteLater，兼顾原生左右两侧。保留当前关闭行为与自定义图标。 |
| 编辑占用 | 高亮五个正则共享一次编译的模式，颜色每次按主题取值；查找替换面板首次 Ctrl+F/H／showFind 才创建。文档、撤销、高亮与现有 DeferredDelete 回收保持，不以禁用功能或 trim 降占用。 |

## 构建与回归

Release 命令 `cmake --build build --config Release --parallel 8` 成功。内存优化后生产代码未再修改；随后只增加独立诊断与文档，未重复已经通过的全套检查。

| 套件 | passed | failed / skipped | 报告文件 |
| --- | ---: | --- | --- |
| core | 9 | 0 / 0 | core.txt |
| editor_groups | 29 | 0 / 0 | editor_groups.txt |
| terminal | 17 | 0 / 0 | terminal.txt |
| processes | 7 | 0 / 0 | processes.txt |
| controls | 6 | 0 / 0 | controls.txt |
| loopbackTerminalLayoutDoesNotRestart | 3 | 0 / 0 | rabbitshell_credential_tests.txt、pty-final.txt |
| memory baseline / optimized（各） | 3 | 0 / 0 | memory-baseline.txt、memory-optimized.txt |

计数含各套 init／cleanup；相关 CTest 5/5 通过，日志 `ctest-final.txt`。内存 probe 是独立诊断目标，不进入常规 CTest。

新增紧凑永久回归覆盖：两个组按钮明确作用所属组；菜单期间组保护及工具区拒绝拖放；窄组长页签不覆盖按钮；左右原生关闭按钮 20 轮开关／主窗口替换不积累；惰性查找首次创建、面板复用、Esc、主题与文档／高亮器销毁。原有保存保护、动态分组、拖影与终端迁移用例继续通过。

控件回归验证晚索引的上下弹出、屏幕右边缘与小空间限高，Esc 不提交且重开后 Down→Enter 选择正确的一项，鼠标／键盘只发一次原信号，窗口移动关闭。终端用输入 signal spy 检查 Tab／Shift+Tab 精确字节、连续与自动重复、终端焦点保持，以及普通控件正常 Tab 遍历。

活 PTY fixture 仅使用随机本机端口的真实 SSH PTY echo，验证移入编辑区、分组、浮窗、收回和移回后原通道／控件身份与字节连续，不重启 session。它不执行真实 shell，不能作为 Tab 补全、SIGINT 后提示符或真实服务器行为的实测依据。

## 两主题生产控件证据

| 内容 | Dracula | GitHub Light |
| --- | --- | --- |
| 文件页签同行工具／窄组 | inline-tabs-dark-narrow.png | inline-tabs-light-narrow.png |
| 下拉框收起／焦点与禁用 | combo-dark-closed.png | combo-light-closed.png |
| 下拉框展开／圆角边框和留白 | combo-dark-open.png | combo-light-open.png |
| 进程纯卡片完整页 | process-dracula-full.png | process-github-light-full.png |
| 进程窄侧栏 | process-dracula-mini.png | process-github-light-mini.png |

文件都在本轮报告目录。截图为生产控件 QWidget::grab；popup 图将真实父窗口和原生 popup 的 grab 按坐标合成，没有绘制替代控件或操纵桌面。主代理已审查上述 UI，确认同行居中、菜单箭头、两主题 popup 边框／圆角／内边距和纯卡片；本轮不再扩大 UI 范围。另保留终端亮灭／形状图、分组／拖影／面板图。offscreen 不能代替 OS 标题栏、混合 DPI、真实屏幕跨屏与原生鼠标拖放验收。

## 内存基线、优化与对象生命周期

先保存 UI 最终状态下的 baseline 二进制与关键源码哈希，再修改三项内存实现。两版 `tests/probe_editor_memory.cpp` 完全相同，SHA-256：

`2B1A8CD1EC78AE9CE0ADC037747EDCD76079A6CD53986FBA00C5AF4BB8C94D33`

| 保留的独立 probe | SHA-256 |
| --- | --- |
| memory-baseline.exe | 88E930ADC6661A562B05B1B59B86C2C34EA954C41C4B668CBF301C3BA1EE96D9 |
| memory-optimized.exe | 22CEE695055EC791C3FD263AA6FA12E4DBCB9CC6608217FE7752D0BF4BD57F58 |

每版单独新进程运行：先分配固定源缓冲，三轮预热，再 25 轮，每轮打开 128 KiB 与 1 MiB 文件，预热末轮及测量第 9／19 轮将大文件换为 4.75 MiB。走真实 Workspace::closePage 和 DeferredDelete；轮换同组开关、跨组迁移／合并、浮窗／收回，包含编辑／undo、查找显示／隐藏、一次修改后 Discard，以及每轮主窗口页签按钮替换。使用独立 fake SecretBackend 和 demo workspace，无网络 session。记录 Windows PrivateUsage、WorkingSetSize、句柄数与控件计数；未调用 EmptyWorkingSet、malloc_trim 或周期强制清理。

以下 MiB = 1,048,576 bytes；Private Bytes 与 Working Set 是不同口径，不相加。

| 阶段 | Private Bytes MiB | Working Set MiB | widgets | 编辑页签按钮 | 主窗页签按钮 | handles |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| baseline warm | 24.027 | 44.793 | 269 | 13 | 8 | 250 |
| baseline final close | 25.785 | 46.777 | 362 | 80 | 33 | 250 |
| optimized warm | 23.758 | 43.895 | 253 | 2 | 3 | 250 |
| optimized final close | 24.344 | 45.254 | 254 | 2 | 3 | 250 |

| 观测 | baseline | optimized |
| --- | ---: | ---: |
| 最后十轮 close Private Bytes 范围 MiB | 24.836–26.395 | 24.277–25.180 |
| 最大采样 Private Bytes MiB | 53.902 | 53.262 |
| probe 最后采样 elapsed 秒 | 29.393 | 3.629 |

明确修复的是被替换的按钮持续保留。优化后每轮关闭按钮固定为 2／3（含原生滚动按钮和仍在使用的主窗关闭按钮）；两版每轮 Editor、QTextDocument、QSyntaxHighlighter 和全部跟踪的 editor/code/document/highlighter/浮窗/当前关闭按钮 QPointer 关闭后均为 0，说明本场景中文件主体原本正常回收。惰性查找减少未使用文件的控件，高亮正则复用避免逐行重新编译；本轮未分别隔离三项改动的贡献，不能把全部内存差额归于某一项。

优化后 warm 到 final 仍有约 0.586 MiB Private Bytes 差额，末十轮并非固定单值。三轮预热含大文件，但首次实际查找、浮窗和修改确认分别发生在后续轮次；分配器、字体与 UI 预热因素没有逐项分解。时间是这一次固定合成文本 probe 的总耗时，不能宣传为整个应用的性能倍数；最终占用差异也不是大幅压缩总内存或所有库零泄漏证明。

原始逐轮数据：memory-baseline.json、memory-optimized.json；字节级汇总与 CSV：memory-comparison.json、memory-samples.csv；源码哈希：memory-baseline-sources.json、memory-optimized-sources.json。`analyze-memory.py` 只使用 Python 标准库，可重新生成汇总，不修改原数据。

### 对单个额外 widget 的定向确认

原综合 probe 两版都在首次修改后 Discard 的第 12 轮出现一个额外 widget，此后稳定。为核对是否按次增长，另建 offscreen 诊断并重复五次真实 dirty close，原 probe 源码、二进制和 JSON 均保持不变。

- 简单 dirty-close 场景：起始及五次结束均为 253，`newSinceInitial` 为空，五次 QMessageBox／Editor／document／highlighter 均销毁，主窗销毁后 widgets=0。证据 dirty-close-diagnostic.json（同 dirty-close-simple.json），对应 dirty-close-diagnostic.cpp／exe。
- 补充重建综合第 12 轮的分组／查找状态：五次结束均为 254，同一个隐藏顶层 QWidget（class=QWidget，objectName 为空，parent=null，640×480）持续存活，未逐轮增加；窗口销毁后该单一 widget 仍在测试进程中。新增列表中的主窗 QToolButton 是正常替换的当前按钮，首轮旧按钮在后轮已销毁，不是累计保留。证据 dirty-close-with-find.json／cpp／exe。

未定位这个隐藏 QWidget 的内部来源，不将它武断归为某个 Qt cache。有限次数未观察到按次增长，主代理接受该边界，不据此扩大生产修改或做全库泄漏结论。诊断工程位于 build/test-results/dirty-close-diagnostic，未接入生产测试开关或常规 CTest。

## 复现命令

在仓库根目录执行，报告环境变量必须是绝对路径，避免 CTest 的 build 工作目录歧义：

```powershell
$env:PATH="$PWD/.tools/Qt/6.8.3/msvc2022_64/bin;$PWD/.tools/Qt/Tools/OpenSSLv3/Win_x64/bin;$env:PATH"
$env:QT_QPA_PLATFORM='offscreen'
$env:RABBITSHELL_TEST_REPORT_DIR='D:/UserFiles/Documents/ChatGPT/RabbitShell/test-results/0.1.6'
cmake --build build --config Release --parallel 8
ctest --test-dir build -C Release -R '^(core|editor_groups|terminal|processes|controls)$' --output-on-failure
$env:PYTHONPATH='.tools/test-python'
& 'C:/Users/JOKER/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' tests/run_integration.py build/Release/rabbitshell_credential_tests.exe loopbackTerminalLayoutDoesNotRestart

# 顺序独立运行；replay 标签保留正式 baseline/optimized JSON。
$env:RABBITSHELL_MEMORY_LABEL='baseline-replay'
& ./test-results/0.1.6/memory-baseline.exe -o test-results/0.1.6/memory-baseline-replay.txt,txt
$env:RABBITSHELL_MEMORY_LABEL='optimized-replay'
& ./test-results/0.1.6/memory-optimized.exe -o test-results/0.1.6/memory-optimized-replay.txt,txt
```

## 交付与未覆盖项

0.1.6 独立打包至 `dist/RabbitShell-0.1.6`，带本文件及同步后的 README、交互、主题、分组、终端、进程与实现记录。最终 exe／包文件清单哈希和旧 0.1.4／0.1.5 未变核对见仓库报告目录 delivery-report.md、package-verification.json 与 package-manifest.json。没有 commit 或 reset。

本轮未执行 macOS/Linux 构建、真实桌面／混合 DPI／多显示器、真实 shell 补全或 SIGINT、真实服务器长期文件／网络负载、完整认证测试。内存结论限定于 Windows offscreen 单次同场景前后测量，不保证任意插件、Qt 内部、分配器或长期运行全部无泄漏。
