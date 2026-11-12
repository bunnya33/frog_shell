# RabbitShell Qt 实现记录

## 0.1.23 文件树辅助功能缓存修复（2026-11-09）

现场获准的有界GUI栈在高CPU区间出现UIAutomationCore/COM→qwindows→Qt Widgets；没有read/editor栈。Qt6.8.3文件树按可见逻辑行缓存子项，持久索引在层次变化后可能与缓存键不再对应；互逆契约和NextSibling重复provider已稳定复现。

FileView通过公共QAccessibleTableInterface::modelChange(ModelReset)仅失效辅助子项缓存，覆盖模型结构/布局前后、展开/收起；生产filter/render变更前显式清理，兼容QSignalBlocker。无业务model reset、树重建或全局关闭辅助功能。析构先断开模型/自身回调，防止基类销毁模型时触及已析构成员。旧ID16轮回收、单击/打开MD、离屏和隐藏Windows回归通过；完整用户现场未重现，边界见 [0.1.23 验证](RabbitShell-0.1.23验证记录.md)。

## 0.1.22 展开前未响应诊断（2026-11-09）

用户确认点击目录左侧小三角、尚未展开即卡住。最后一次 RenderEnd 可能来自上一项，不能归因到失败点击或树渲染。真实鼠标箭头、滚动嵌套/相邻目录、排序及生产 WindowStyle 的隐藏 Windows 用例未复现，未修改目录算法或宣称修复。

DiagnosticApplication 通过公共 QApplication::notify 的栈作用域仅更新固定类型元数据，嵌套返回恢复外层；后台 UiStall 才写当前 event/type/parent/depth/age，不逐事件落盘，也不保留接收对象指针。补鼠标 hit/base press/release/expanded、monitor/process snapshot、Terminal feed/resize 的 begin/end。原有有界队列、离锁批量写盘和隐私白名单保留。人工阻塞仅证明诊断有效，不是用户 Hang 复现；验证范围和限制见 [0.1.22 验证](RabbitShell-0.1.22验证记录.md)。

## 0.1.21 上传预览与删除诊断（2026-11-09）

FileView 保持 .20 的目标解析，预览改为实际目录的可见子树，根目标框住 viewport 与顶部路径；可见行遍历/裁剪，不扫离屏整棵树，不改选择。render 在 QSignalBlocker 前清理预览，避免路径框残留。

新增独立本地日志 worker，结构化白名单事件与路径哈希，队列/轮转有界，离锁批量写盘；心跳与后台 watchdog 可记录 GUI 停顿时的最后 UI 阶段。用户确认删除后的 Hang 仍未复现；仅添加诊断，不对删除算法作猜测性修改。真实隔离 SFTP 生产阶段序列及人工3.2秒阻塞分别验证。详见 [0.1.21 验证](RabbitShell-0.1.21验证记录.md)。

## 0.1.8 终端选择、删除、路径与任务速度（2026-11-04）

终端激活统一为 activateTerminal，键盘选行保留列表焦点，focus 反向保选用 QSignalBlocker。管理树按 ID 增量更新，选中行共用两个 overlay 按钮并在 pressed 捕获目标 ID，滚动／折叠／绘制更新位置；关闭稳定选择同组相邻项，非活动关闭不改活动终端。移除 PTY 启动／关闭 processSnapshot、terminal-status、tokens、跟踪状态、terminalprocess 模块及专属测试；单终端立即关闭。标题与颜色动作移除，重命名同步总栏／列表／浮窗。Workspace::movePage 和 EditorArea 入口均拒绝 terminalPage；浮窗返回原组并保持同一 Terminal、ID 与 PTY。

删除提取 deleteSelectedFiles／deleteEntries，事件仅归真实文件树焦点；确认捕获路径值及 QPointer<Session>，返回后核对在线、会话身份、关闭状态。面包屑空白左击切编辑，段按钮仍导航，Esc／失焦只恢复，删除路径按钮与底部说明块，不创建全窗口 Ctrl+L。

Session::TransferMeter 按每次 perform 的任务尝试开始，使用 QElapsedTimer；成功 SFTP read／write 累加 payload，目录递归共享，已跳过不计。有效耗时包括枚举／提交、扣除 worker 的显式暂停等待；120ms 有界单区间采样，暂停前后重置，终态强制回报，结果携带 payloadBytes／activeMs，最短按 1ms 分母格式化平均。失败／取消也回报本次计数但不会发成功结果。统计存于任务 item 随历史 trim 释放，重试和断线清除实时展示；完成主动清除总速率，不依赖下一任务启动。

Release、相关 6 套 CTest、SSH/SFTP 及存活 PTY fixture 与两主题截图通过。详见 [0.1.8 验证记录](RabbitShell-0.1.8验证记录.md)。以下 0.1.7 关闭检测及早期终端迁移叙述保留历史语义，其规则已被本版覆盖。

## 0.1.7 局部文件树、终端面板与连接偏好（2026-11-04）

删除原先刷新根目录造成其他枝重建；现在操作 ID 关联原路径，仅同步受影响父目录及递归部分失败涉及的已加载子目录。路径身份复用 item，目录／链接类型变化清掉旧子树；持久索引、按路径最新请求和导航 generation 拒绝迟到结果。新建改为固定右键目录的树内占位，Enter／非空失焦只提交一次；明确导航／替换草稿入口先 finish，失效／断线入口 cancel，非法／同名保留修正。编辑区跨列铺开并按实际主题行高布局。

终端面板拆分固定横向，新增仅右侧最大化／还原与高度恢复。进程 header 增加局部短竖线，全部列可拖动；full／compact 的列宽分别通过一次性 250ms 合并写入，析构补写未完成偏好。网卡按 profile.id 保存名称，名单未变不重建，程序回退不覆盖用户选择。

关闭检测不改 channel_shell：开 PTY 前后用独立 exec 读取 Linux /proc，按 sshd／sshd-session 的 comm、PID/starttime 和祖先拓扑建立唯一新 TTY shell 身份；关闭时验证新快照、可执行文件、关联进程及可解释等待状态。近期／pending／变化输入保守 Unknown，Busy／Unknown 默认取消，重复关闭去重，失联清理弹窗。短超时与不完整输出不主动判死会话；实际 socket 故障仍由现有读写／保活处理。该能力不证明所有内建命令空闲，也不等于真实 OpenSSH 已验收。

最终 Release 与相关 CTest 7/7 通过，core 9／editor_groups 29／terminal 17／processes 8／controls 6／workspace 11／terminal_process 5；指定布局 PTY 与超时、截断两种独立 fixture 各 3 项通过（含 init/cleanup），均 0 failed／0 skipped。WSL 运行生产脚本取得真实 Bash/PTY 快照并由生产分类器验证，sshd 祖先拓扑由 fixture 模拟。两主题新建、表头和最大化／还原生产截图已审查。详见 [0.1.7 验证记录](RabbitShell-0.1.7验证记录.md)。以下旧版本记录保持历史语义。

2026-11-03。用户确认按统一原型的功能与样式开发，并指定 Qt，优先降低内存占用、支持跨平台。本记录替代早期方案中的 Electron 建议；功能目标仍以《交互设计检查清单》和统一原型为准。

0.1.1 已根据用户提供的 Qt 桌面规范更新原生标题栏、窗口恢复、文件浮窗按钮、主题预览和控件状态。实现取舍及本轮验收范围见 [Qt 窗口规范落实](RabbitShell-Qt窗口规范落实.md)。

0.1.2 增加“保存密码／保存私钥口令”。普通配置仅保存 `rememberSecret` 偏好，凭据由平台安全库保管；新建/编辑与运行时登录均支持。交互约定见 [保存密码设计](RabbitShell-保存密码设计.md)，逐项证据与平台范围见 [保存密码验证](RabbitShell-保存密码验证.md)。

0.1.3 增加动态编辑分组、真实 Qt 页签拖放、可辨的 frame 分区和统一矢量图标按钮。左右分屏连续增加同排列；明确选择上下才建立上下布局。空组自动移除，新文件进入活动组，全部合并归活动组。规则见 [编辑分组与面板设计](RabbitShell-编辑分组与面板设计.md)，测试和截图范围见 [编辑分组验证](RabbitShell-编辑分组验证.md)。

0.1.4 移除原生标题栏下重复品牌／身份／设置的 appHeader 整行，删除冗余 identity 成员；原生 windowTitle 继续更新。设置搬到服务器页签栏右端，与连接管理加号并排，保留 tooltip、无障碍名称和 Ctrl+,。资源管理改为紧凑目录按钮与无焦点矢量 chevron，当前目录适度加粗，根目录仅 `/`，目录大小写及 `&` 保真；长路径用独立省略标记与最近三级，优先当前目录和完整 tooltip，避免撑宽侧栏。路径输入与现有历史流程沿用，demo 不更新 history 的既有行为未在本轮扩修。

本轮 Release 构建成功，既有 core 9 项与 editor_groups 23 项均为 0 failed / 0 skipped。独立临时 offscreen MainWindow 检查通过两主题服务器页／连接管理页的设置按钮与 Ctrl+,、原生 windowTitle 随页签更新、各层路径点击、真实大小写与 `&`、Enter／Esc／失焦还原、长路径侧栏宽度及浮窗专用 floatingHeader。截图加载真实中文与等宽字体；源实现和 tests 未加入试验入口或永久测试套件。证据见 `test-results/0.1.4/delivery-report.md`。offscreen 不含 OS 合成标题栏，不将 demo 检查作为真实前进／返回或 SSH/SFTP 完整验收；本轮未操作桌面、真实服务器或用户配置，未重跑无关协议测试。保留 0.1.3 已有多列、拖放、frame、图标及凭据功能。

0.1.5 修复终端光标与 Ctrl+C。原先纯修饰键按下就清选区，实际 Ctrl+C 的 Control 前置事件会使首次复制变成中断；现在修饰键保持选区，使用独立有效选区状态，复制保留空格／缩进／中文及反向选择，Ctrl+C 复制后清高亮并忽略自动重复，无选区才回到底部发送一次 0x03。Ctrl+Shift+C 和平台标准复制保持纯复制，右键中断明确为直接发送。libvterm 的 movecursor 与显隐／闪烁／形状属性均触发重绘，默认反色块光标覆盖宽字符；失焦／隐藏／窗口不活动／最小化／回看历史／远端隐藏时停止闪烁，无全局轮询。沿用 16ms 输出批处理与迁移后的最终 resize；不新建终端或重启 shell。实现、证据与验收边界见 [终端光标与复制验证](RabbitShell-终端光标与复制验证.md)。

0.1.5 同时修复监控自动刷新清除进程选择：`monitor()` 原先每轮 `setRowCount(0)` 重建两张表，现使用共享 `ProcessTable` 按 PID 专用 role 复用 item，仅删除退出样本项与新增项；在批量禁重绘、暂停排序时更新，按 PID 恢复选中集合、current PID／列，保留排序列／方向、滚动、列宽与焦点。侧栏只处理当前前五项，离开前五即取消该可见选择；采样命令和 SSH 协议未改变，不声称严格识别 PID 复用。整体进程卡片 8px 圆角、1px 外框，独立底色表头与正文连续，无内部横竖线；两主题交替底色与选中态优先，正文 13px、数值右对齐。详见 [进程刷新与卡片验证](RabbitShell-进程刷新与卡片验证.md)。

0.1.5 最终 Windows Release 构建成功；core 9、editor_groups 23、terminal 16、processes 7，以及 `loopbackTerminalLayoutDoesNotRestart` 的 3 项（含 init/cleanup）通过，全部 0 failed / 0 skipped。主代理审查通过两主题光标亮灭及进程完整表／窄侧栏真实控件截图；未执行可见桌面自动化。最新证据统一归档到仓库 `test-results/0.1.5`，最终包为独立的 `dist/RabbitShell-0.1.5`，保留 0.1.4。fixture 只验证原 PTY 连续性与字节路由，不执行 shell、不声称验证真实 SIGINT 响应；macOS/Linux 未实测。

## 0.1.6 紧凑控件、终端 Tab 与编辑器内存

编辑区删除 `editorHead` 整行；每组 cornerWidget 提供同行的向右分屏主按钮、向右／向下菜单及合并按钮，动作明确归点击组。corner 高度跟随页签行，窄组保留原生页签滚动；拖放检查真实 tabBar 范围并拒绝工具区，组菜单纳入 interactionActive 和 QPointer 生命周期保护。终端头部沿用。进程页删除卡片上方标题和下方说明，移除 processDetail；CPU／RSS 说明改为表头悬停文本，保留 0.1.5 的 PID 刷新与卡片规则。

四类现存下拉框使用共享 StyledComboBox，沿用 Qt 原生 model/view 和 popup 容器，只定制主题绘制、边框、内边距与弹出几何。下方优先，空间不足向上，屏幕范围限宽限高；Esc 不提交且重开恢复已提交 currentIndex，选择信号只发送一次。窗口移动、隐藏或布局变化关闭 popup，无轮询。原生容器实际圆角／边框与同行按钮垂直位置已通过两主题 offscreen 图审查。

Terminal::event 在 Qt 默认焦点遍历之前接收聚焦终端内的 Tab／Backtab，再走已有 libvterm 键盘路径。普通 Tab 为 09，Shift+Tab 为 ESC[Z，可连续和自动重复输入；其他控件继续 Tab 遍历，Ctrl/Alt/Meta 组合沿用原路由。远端负责补全，不客户端插候选、不创建新 PTY。保留 0.1.5 光标、Ctrl+C、IME 和 resize 流程。

内存基线先保存可执行文件与源码哈希，再用相同 probe 对比三项改动。确认 Qt setTabButton 只隐藏旧 widget，EditorTabs 与 MainWindow 的替换路径持续保留原生／旧自定义关闭按钮；共享 replaceTabButton 捕获旧指针并 deleteLater，同时清理左右两侧。编辑文档与高亮器原先已正常销毁，没有盲改 closePage 的延后回收。高亮五个正则改为共享 immutable patterns、动态取主题色；查找面板首次 showFind／Ctrl+F/H 时构建，resize／Esc／查找路径保持空指针安全，撤销和高亮继续启用。

Windows offscreen 独立进程：三轮预热、25 轮 128 KiB／1 MiB 文件开关，少量 4.75 MiB 文件，覆盖移组、浮窗收回、编辑撤销、查找及修改后 Discard。基线关闭后按钮从 warm 的 13／8 增到 80／33；优化后始终 2／3。两版每轮 Editor、文档、高亮器和跟踪对象关闭归零。最终 Private Bytes 25.785→24.344 MiB，Working Set 46.777→45.254 MiB；优化后最后十轮关闭 Private Bytes 范围 24.277–25.180 MiB。结果是有限场景单次进程对比，不能将全部残留归为按钮或宣称总库无泄漏。5 次独立 dirty-close 无累积；补充同查找／移组状态的 5 次有同一个隐藏无父 QWidget 稳定保留，具体来源未定位，未据此修改生产代码。完整对象计数、原始字节及边界见 [0.1.6 验证与内存记录](RabbitShell-0.1.6验证与内存记录.md)。

最终 Release 构建成功，core 9、editor_groups 29、terminal 17、processes 7、controls 6，以及指定活 PTY fixture 3（均含 init/cleanup）通过，0 failed／0 skipped。主代理已验收 UI、交互和内存范围；所有操作均为 offscreen 或独立 loopback fixture，未操作用户桌面、真实服务器、真实凭据或旧 dist。macOS/Linux、真实 shell 补全／SIGINT 和长期真实负载未验收。0.1.6 包使用独立目录，证据绝对路径为 `D:/UserFiles/Documents/ChatGPT/RabbitShell/test-results/0.1.6`。

## 技术决定

- C++20、Qt 6 Widgets、CMake。使用平台原生窗口和 Qt 控件，不嵌入浏览器。
- SSH/SFTP 使用 libssh2 + OpenSSL 3；终端用 libvterm 解析 VT/ANSI，Qt 直接绘制。无需 VS Code Server 或远端常驻代理。
- 同一连接配置对应一个工作区。每个工作区独立管理目录、文件、终端和传输；多个 PTY 通道复用交互连接，传输按需建立独立连接。
- 网络工作在线程中执行，GUI 通过信号接收结果。关闭时异步等待后台清理，避免 GUI 线程直接等待网络超时。
- Qt 动态链接。依赖版本、校验值和许可见 `third_party/NOTICE.md`。

## 已有的原生实现

| 模块 | 当前实现 |
| --- | --- |
| 主窗口 | 固定连接管理页签、多服务器页签、工作区去重、服务器身份、状态隔离、关闭及退出保护 |
| 主题 | Dracula / GitHub Light，设置中立即切换，记住选择，覆盖浮窗、编辑器、终端、菜单和表格 |
| 连接 | 新建/编辑/复制、搜索、收藏、分组筛选、移动分组、配置导出、有限 SSH config 导入；密码、私钥、Agent 入口；主机指纹核验、变化拒绝、断开和重连 |
| 资源管理 | 路径层级、路径输入失焦还原、前进/返回/刷新/主目录；目录展开按需请求；整行选择、Ctrl/Shift 多选、空白区域框选；名称过滤、隐藏文件开关、名称/大小/修改时间排序；新建、F2 重命名、属性、单项权限编辑、删除确认 |
| 编辑 | 原生文本编辑、行号、基础语法颜色、动态编辑组、连续左右拆分为同排多列、显式上下布局、页签排序、组间移动、真实独立窗口；多种关闭菜单；Ctrl+F/H 覆盖式查找替换；保存竞争保护和本地草稿 |
| 文件保存 | UTF-8/BOM/CRLF 保留；二进制拒绝编辑、非 UTF-8 只读预览、5 MiB 上限；保存前后检查远端内容哈希；临时文件写入后原子替换；冲突时本地/远端并排查看，可保留草稿、重新载入或确认覆盖；覆盖仍校验刚展示的远端版本 |
| 终端 | 真实 SSH PTY、ANSI/颜色/中文流式解析、键盘和 IME 输入、resize、多会话和分组拆分；新建/终止/重命名/颜色/浮窗/编辑区；单会话隐藏管理栏；复制、查找、清屏、中断、多行粘贴预览 |
| 监控 | Linux CPU、内存、根磁盘、运行时间；进程 CPU/RSS/状态与排序；网卡独立收发速率和曲线；左下角 SSH 请求耗时和连接颜色 |
| 传输 | 右下角上传/下载面板；多选与目录批次、子文件明细、拖入上传；保存位置、同名策略、64 KiB 分块、整批暂停/继续/取消；部分失败汇总、从头重试、断线等待和重连重排；本地及支持 statvfs 的远端空间预检；不递归跟随符号链接 |

上述“已有实现”表示已接入代码，不表示原型每项细则已经完成跨平台验收。`--demo` 单独标明模拟数据，正常运行不预设服务器、假连接或假保存。

## 已验证

- Windows 11、MSVC v143、Qt 6.8.3，Release 构建成功。
- 自动测试通过：配置往返、Shell 参数转义、终端 ANSI/中文分包和 Ctrl+C、终端历史上限、主题切换保留编辑内容、保存返回时保留后续修改、重新载入更新编码/哈希、浮窗内容可见及隐藏/恢复/收回。
- 独立本机 SSH/SFTP 测试服务通过：握手、首次指纹信任与取消、指纹变化拒绝、认证失败、PTY 字节收发、目录列表、读取、冲突拒绝、原子保存、二进制上传/下载一致性、重命名、递归删除、目录失败汇总、重名跳过、排队取消。
- 协议测试使用 Paramiko 本机隔离服务，不执行真实 shell 命令；未连接用户真实服务器。单独的 CTest 网络用例没有端口时会跳过，本轮另通过 `tests/run_integration.py` 实际执行协议测试（0 failed / 0 skipped）。
- 早期桌面检查已切换 Dracula / GitHub Light，并发现及修复浮窗空白、文件页签关闭按钮不显示、浅色状态栏对比度问题。浮窗修复增加了自动回归测试。
- 0.1.0 打包版本已成功启动。该轮最终桌面自动检查被用户按 Esc 中止，未再操作界面；0.1.1 的窗口检查范围另见窗口规范落实记录。
- 2026-11-03 11:21，Windows Release 打包程序离线预览（两个小文件、两个终端）的连续三次采样：私有内存 **76.95 MiB**，工作集 **143.84 MiB**。这是当前进程快照，不代表真实连接、长时间运行或其他平台的内存上限。完整运行目录约 **31.78 MiB**。

## 资源边界

| 项目 | 当前限制 |
| --- | --- |
| 每个终端滚动历史 | 2 MiB 或 2,000 行，先达到者生效；当前屏幕另计 |
| SSH 输出待投递量 | 每工作区约 256 KiB，消费后确认，防止 GUI 消息无界积压 |
| 请求队列 | 最多 256 项 / 8 MiB 数据；重复 resize 合并 |
| 文件读写 | 传输缓冲 64 KiB；编辑文件上限 5 MiB |
| 文件目录 | 单目录最多加载 10,000 项；超限明确停止，不假装展示完整目录 |
| 监控 | 活动工作区每 2 秒，后台每 10 秒；进程最多 200 项；曲线只保留短期样本 |
| 传输任务 | 最多 256 个未完成根任务，保留最近 100 个已结束根任务；每个根任务最多展示 50 条子文件明细，汇总保留总数；传输连接空闲 60 秒后释放 |
| 视图 | 移动/分屏/浮窗复用现有控件，不重复载入文件内容或新建 SSH 会话 |

调优依据为 Release 的私有内存、工作集、CPU 和交互延迟，不以“选择 Qt”替代测量，也不承诺固定内存上限。大字体、超宽终端、多个编辑撤销栈、SSH 加密库和系统字体缓存都会增加实际用量。

## 与完整原型之间仍需补齐的工作

这些是后续开发和验收事项，**没有删减用户已经确认的目标**。

1. 连接管理：完整分组增删改/拖动排序、测试连接入口、跳板机、完整 SSH 配置导入差异确认，以及各系统 Agent/加密密钥/动态验证组合。系统凭据库已接入，macOS Keychain/Linux Secret Service 的构建与实机验收仍待补齐。
2. 文件：删除前递归项目计数、递归权限与普通文件执行位保护、链接目标展示、只读权限的完整所有者判断、目录重命名与相关终端路径协调，以及更大目录的分页模型。
3. 编辑：编码选择；5 MiB 以上文件只读分页预览；完整语法服务。冲突对比目前为并排文本，专用差异高亮属于后置增强。
4. 终端：与各 shell 的命令提示/历史补全协作、日志级别自动高亮、图标选择、前台任务识别、鼠标协议和完整 IME 组合态等兼容性。当前 Tab 补全来自远端 shell，不伪造命令补全；多行粘贴先暂存，用户 Enter 后发送，服务器是否立即执行由 shell 决定。
5. 传输：实际网络断开/重连及整批暂停的界面联合验收；超大目录子文件明细的按需查询。当前重试从头传，不声称断点续传；目录根任务部分失败后的重试会重新遍历整个根目录，继续采用所选重名策略。传输连接单独断开时可右键重新连接，不自动无限重试。
6. 会话与可靠性：长时间持续输出/网络抖动/大量文件压力测试；目前目录遍历和监控命令属于交互 worker 中的有界阻塞操作，慢服务器上仍可能推迟终端数据读取，需要进一步拆分调度。
7. 平台交付：Windows 干净机器、macOS（含 Apple Silicon）、Linux（X11/Wayland）构建和实机验证；快捷键/字体/DPI/跨屏窗口；安装包、签名和依赖安全补丁更新。

## 实际边界

- SSH 耗时为监控请求往返耗时，包含远端执行时间，状态栏 tooltip 已注明。不能将其当作纯网络 RTT。
- 普通 SSH 重连不会恢复之前的终端进程；保留历史输出和编辑草稿。
- 原子保存要求服务器支持 `posix-rename@openssh.com`。不支持时保留原文件与草稿并提示失败，不先删除原文件来模拟替换。SFTP 无通用原子 compare-and-swap，哈希检查仍存在最后校验到重命名之间的竞争窗口。
- 保存临时文件时保留原 POSIX 权限和 UID/GID；ACL、扩展属性与硬链接语义尚未完整覆盖。
- 后台 Linux 监控命令固定，目录参数进行 Shell 单引号转义；SSH config 导入不执行 ProxyCommand 或 Match exec 等脚本。
- 0.1.2 凭据存储采用 Windows Credential Manager / macOS Keychain / Linux 可选 libsecret。Windows 实测通过；系统库不可用时清楚报错且不做明文回退。存储操作由后台队列串行执行，修订号撤销旧认证的晚到保存；保存期间禁止取消表单，退出异步等待完成。跨系统、强制结束/断电场景仍需各平台验收，系统库与 JSON 之间不宣称具备跨存储的崩溃原子事务。

## 0.1.3 布局、拖放与视觉实现

- `EditorArea` 管理动态 `QSplitter` 树和活动 `EditorTabs`。同向新增直接插入同一容器，仅平分目标原空间；不按组数切换方向。空组在迁移结束后清理，只有一个子节点的 splitter 折叠，最后保留一个空编辑区。页签、编辑文档、撤销栈和 PTY 控件复用。
- `QDrag/QMimeData` 提供小页签拖影、页签插入线与内容中心／四边的透明预览。MIME 同时验证本次拖放实例、所属区域、源组和页面存活。全局事件过滤器仅处理所属目标，不吞掉其他服务器事件。拖放嵌套循环用 `QPointer` 防止关闭导致悬空访问，清理延后到循环结束；关闭菜单有独立交互保护。
- footer 直接标记为 `footer`，不用无法覆盖它的 QStatusBar 规则。主要面板、卡片、分隔、活动组、drop 使用集中语义色；共享边线由分割手柄负责，视觉线 1、命中宽 7 逻辑像素。系统标题栏和顶层外框继续由原生窗口负责。
- `Icon` 枚举和 QPainter QIconEngine 覆盖工具栏、活动栏、查找、页签关闭和浮窗返回。按钮使用真实 tooltip／accessibleName；独立 Add 可见笔画约 15，16px Close 的叉约 12 逻辑像素。保存／取消等表单文字动作保留。
- 终端 resize 合并到下一轮事件循环，在最终可见布局下更新 PTY，避免迁移的临时小尺寸截断输出；不改变终端 reflow 策略。真实本机 SSH fixture 验证移组、拆分、浮窗、收回后原通道仍可 echo，未出现重复 open-terminal 或终端关闭。
- 离线 demo 初始为四个合成文件／三列，右列保留两页，可继续执行左右拆分得到四列。两主题默认截图为三列／四列单排；显式上下嵌套仍有行为测试，默认演示不使用网格。

0.1.3 最终回归：core 9、editor_groups 23、SSH/SFTP fixture 3、credentials／活 PTY 布局 fixture 14，均 0 failed / 0 skipped。常规 CTest 的无端口 SSH 项单独 skip，已用 fixture runner 实际执行。主代理原生审查确认拖影／中心与右侧预览／Esc、同排三列四列／中列移空收起／全部合并及内容保留；工具 drag 需追加同点 click 才释放，单手势时序未证实，不标为全面通过。正式包 exe 与 preview SHA-256 一致，证据和细节见编辑分组验证。

## 0.1.16 内容字体设置与终端缩放

共享lazy ContentFonts服务在测试/数据目录配置后才读QSettings，默认Cascadia Code NF Medium500、三类14逻辑px。QObject role信号以控件为context，创建时只绑定一次；即时同步隐藏/浮窗及新控件。内置variable轴显式设定，SGR bold同步至少700；真实400/500/700墨迹比较通过。设置滚动且受屏幕约束，保留系统UI字体与StyledComboBox，支持恢复默认。

精确物理Ctrl+滚轮/↑↓只由实际正文焦点处理，拦ShortcutOverride，避免Qt内置point zoom或终端发送字节。端口/服务布局根据度量更新，40px内部横滚，option.font绘制；设置保护草稿，不触发业务变化。主屏重排用固定0.3.3的CMake生成screen/state窄补丁，原源与MIT保留；宽字、空格、组合字符、历史、超屏spill、DEC saved/phantom wrap及选区序数映射均有失败复现和修复证据。

最终受影响CTest7/7通过，appearance23、terminal18、editor_groups29、core9、workspace16、ports9（2 fixture skip）、services11（6 fixture skip），均0失败。offscreen双主题设置默认/40px列表与正文截图实际查看；无真实远端/桌面/DPI/跨平台及新压力矩阵验收。边界与证据见[0.1.16验证](RabbitShell-0.1.16验证记录.md)和[重排实现](RabbitShell-0.1.16终端重排修复方案.md)。
