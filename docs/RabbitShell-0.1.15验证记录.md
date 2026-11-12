# RabbitShell 0.1.15 验证记录

日期：2026-11-05。Windows x64 Release，Qt 6.8.3、MSVC；本版只调整终端/编辑器内容字体和颜色，不改 SSH、凭据、known_hosts、systemd 或 launcher 执行核心。

## 实现

用户提供的 `CascadiaCodeNF.ttf` 原字节内嵌 Qt resource。文件 **2,548,236 bytes**，SHA256 **16d00fae9fc289cda8c9ee4c578ccb4d410f3fa54ec0e5a3901c059b0248a970**；实际 family `Cascadia Code NF`、Regular、PostScript `CascadiaCodeNF-Regular`、Version 2407.024。共享 `contentfont` helper 由组件直接调用，首次使用时进程内仅注册一次，测试不依赖 main 初始化或 Downloads 路径；没有每终端字体字节副本。

默认 14 Qt 逻辑 px，抗锯齿、完整 hinting、保留系统字体合并。该文件无中文字形，中文回退系统字体，终端按 libvterm 双格/组合字符定位；Nerd 字形 U+E0B0、U+F120 来自原字体。终端关闭 liga/clig/dlig/calt 并逐格绘制/裁切，文件编辑保留 Qt 文本布局。正文和行号共用字体；UI/Find 浮层保持系统字体 13px。全局字号改由 QApplication 默认值控制，显式内容字体避开 QSS 覆盖；主题样式重置前后用短生命周期 QPointer/QFont 快照保留当前内容字体。

终端字体/样式/DPI 事件重算格宽、行高、行列与 IME 光标，编辑器同步 tab、行号宽度及位置。终端绘制只处理脏区域相交的可见行，循环外复用单行 cell 缓冲，按行映射 Unicode/列并计算高亮跨度，不持有历史高亮缓存；既有历史上限不改。这里是共享/有界算法设计与功能验证，不是重新测过整体内存压力矩阵的结论。

16 色 ANSI palette 按已固定的 Dracula 与 GitHub Light 官方来源独立配置，恢复 yellow 与 bright 语义，不用 UI 状态色替代；保留 256 色及 truecolor。默认色按 cell 的 DEFAULT_FG/BG flags 绘制时解析；只调用 state 的默认色设置，避开 libvterm 0.3.3 screen setter 从当前显式 pen 污染默认 cell 的路径，历史/当前屏幕一致。

保守增强仅用于默认前景/背景、普通属性的完整命中段：常见行首 `user@host:~/cwd$` / `user@host:/cwd#` 分段配色，以及独立大写 INFO/WARN/ERROR/FATAL 标记。远端显式 ANSI/背景/反色/其他属性、选区及光标优先，alternate screen 不增强；只修改绘制，不改 feed/input/文本/复制，不注入命令。它是有限文本启发式，可能漏掉其他提示符，也不是 shell 语法或日志语义解析，更不提供补全。

编辑器基础 token 补充数字/布尔、函数和 JSON 属性；由词法扫描最终覆盖字符串/注释，正确处理字符串内 #、//、块注释标记和转义引号，保留块注释/三引号的简单跨行状态。扫描用 QStringView/字符比较，无每字符 QString 切片分配。未引入 LSP、WebEngine、新的语言框架或设置项。GitHub Light 正文统一为 #24292E。

## 许可与零安装

包内 `licenses/CASCADIA-CODE-OFL.txt` 为精确 tag 原文：https://raw.githubusercontent.com/microsoft/cascadia-code/v2407.24/LICENSE 。NOTICE 保留原字体 © 2021 Microsoft Corporation 版权及官方 OFL/Reserved Font Name 信息；`docs/cascadia-code-source.json` 保留实际 name table、原文件 hash 与来源，源码对应 `resources/fonts/source.json`。不依据 NF 文件名推断其他补丁来源。字体随应用提供，无需用户安装字体或依赖字体工具；远端仍零安装。

## 实际验证

全部使用临时配置、QStandardPaths test mode、Qt offscreen；未连接真实服务器、操作真实凭据/known_hosts 或可见桌面。本版不需要网络 fixture。首个资源字体测试 3/0/0；最终以以下定向结果为准：

| 目标 | passed | failed | skipped |
| --- | ---: | ---: | ---: |
| appearance | 10 | 0 | 0 |
| terminal | 18 | 0 | 0 |
| editor_groups | 29 | 0 | 0 |
| core | 9 | 0 | 0 |
| controls | 6 | 0 | 0 |
| workspace | 16 | 0 | 0 |

共 6 个 CTest 目标通过、0 失败；报告 `test-results/0.1.15/ctest-targeted.txt` 与各 `*-Release.txt`。覆盖真实 Qt resource hash/family、等宽与 Nerd/CJK 回退，字号变化、中文/组合字符/选区/IME、主题对象身份与内容不变，16/bright/256/truecolor 及默认色优先级、alternate screen 禁增强，字符串/注释/函数/属性、撤销/保存格式与保存竞争，以及既有终端交互、编辑分组和 Workspace 生命周期。

新增主题切换回归先输出默认/显式/256/truecolor，并保留当前显式红/蓝 SGR pen；分别核验 screen/history 切换后默认色与显式色，再 feed 显式文本及 SGR0 默认文本。初轮 appearance 为 **9/1/0**，准确发现主题 repolish 把运行时 20px 恢复成初始 14px；保留 `appearance-initial.txt`。修复字体快照后原 20px 断言不降低，最终 10/0/0。初次测试源码的自定义 raw string 也触发 moc 解析/链接错误，改为等价普通字符串后构建通过。

另以 `QT_SCALE_FACTOR=1.5` 只运行字体度量/双主题证据，**5/0/0**，报告 `appearance-dpi-150.txt`。实际离屏 DPR 为 1 / 1.5，运行时 20px 下格宽 12、行高 26；默认内容截图为 14px。包含 DPI 事件模拟和离屏缩放，不等于真实屏幕切换、多显示器、平台 IME 候选窗或真实 DPI 验收。

已查看实际生产 Terminal + Editor 内容控件的 `appearance-dark.png` / `appearance-light.png`，以及 `dpi-150/` 同名图；不是完整工作区截图。正文、中文、Nerd 图标、行号、提示符/日志等级与两套 ANSI 均实际渲染。原始色板中的低对比 ANSI 值仍按远端语义保留。Qt offscreen 字体目录/propagateSizeHints 等平台警告保留，不将其转述为功能失败或桌面验收。没有重跑无关 SSH、全套压力或旧 launcher 27 项矩阵。

构建：`cmake -S . -B build`，Release 目标 RabbitShell、rabbitshell_appearance_tests、rabbitshell_terminal_tests、rabbitshell_tests、rabbitshell_editorarea_tests、rabbitshell_workspace_tests、rabbitshell_control_tests。定向命令：`ctest --test-dir build -C Release -R "^(appearance|terminal|core|editor_groups|workspace|controls)$" --output-on-failure`；仅为测试进程配置 Qt/OpenSSL PATH 与临时输出目录。

## 交付与边界

新包目标为 `dist/RabbitShell-0.1.15` 与 `dist/RabbitShell-0.1.15-portable.exe`，保留现存 0.1.12 / 0.1.13 / 0.1.14 的三个目录及三个 EXE。目录使用 `scripts/package-windows.ps1 -Destination ./dist/RabbitShell-0.1.15`；文档/许可同步后 pin inventory，再显式执行 `scripts/package-portable-windows.ps1 -Version 0.1.15 -Source ./dist/RabbitShell-0.1.15 -Manifest <inventory>`。portable 脚本兼容默认仍为 0.1.12，本版不使用省略参数的旧默认流程。

目录核验使用 verify-package，portable 静态使用 verify-portable；最终单 EXE 只做既有 --version/manager/demo 三项隔离烟测：最小 PATH、临时 profile/cache/data、仅此 EXE 的中文含空格空目录、隐藏/offscreen 进程。manager/demo 由 harness 启动后终止其所属子进程并核对 launcher 转发退出码，不是可见 UI 正常退出验收。最终实际结果、包尺寸/哈希/内部 app hash、源文件清单与旧包 aggregate 复核写在包外 `test-results/0.1.15/final-report.json` 等证据中；本文在 pin 前冻结，不把打包后报告哈希与 payload 来源混用。

未测真实服务器/systemd/SSH 凭据、macOS/Linux 客户端、真实桌面/多显示器/物理 DPI/长期内存压力。基础高亮不保证所有语言或 shell 的完整语义；中文依赖 OS 可用回退字体。既有业务和安全边界继续适用。
