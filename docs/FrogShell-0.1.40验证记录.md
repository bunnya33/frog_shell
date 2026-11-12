# FrogShell 0.1.40 验证记录

本版修复终端调整窗口大小后跨历史区软折行不能恢复的问题，并完善编辑器普通光标行、文字选区和查找定位三态。详见 [实施说明](0.1.40-implementation.zh-CN.md)。

## 行为与兼容

终端按权威 continuation 元数据合并或拆分逻辑行，历史和可见主屏之间的接缝一起处理，不根据满列或空格猜测。程序主动换行、连续空行和真实尾空格保持原样；宽字、组合字符、ANSI 属性及 DECSCA 受保护单元格随重排保留。光标、pending wrap、组合字符锚点、选择与滚动锚点随内容映射。备用屏保持原生固定网格，备用屏期间的主屏也可重排。历史仍限 2000 行 / 2MiB，元数据和位图计入限额，重排暂存有界。

本轮不修改 SSH 会话和 PTY resize 路由，不清屏、不重启终端、不重发命令、不额外发送输入。只能重排已经收到的内容，不能重新计算 PM2 历史表格的业务列宽。现有清屏保护继续生效。

编辑器聚焦且无文字选区时显示一物理像素的当前行细框；文字选择优先；查找定位时显示淡色行底及棕黄命中，当前命中更突出。手工移动或选择、关闭或清空查找、无结果及无效正则会恢复相应状态。查找输入及替换输入的 Return/Enter 不再冒泡改写正文，Shift+Return 保留反向导航。未改变字体、行距、文档格式、dirty 状态与撤销栈。

连接、收藏、密码、应用锁、本地设置及单实例身份继续兼容 RabbitShell；上传暂存、原子替换及重连规则不变。目录包和便携启动器均使用现有 FrogShell 透明青蛙图标。

## 验证结果

Windows x64、Qt 6.8.3、MSVC Release，隔离配置、offscreen 实际控件及合成 VT 输入，以下数量包含 init/cleanup，均零失败、零跳过：

- Terminal 全套 39 通过：跨历史表格 180→72→180、历史/屏幕软行接缝、备用屏期间主屏重排、精确满列后 CRLF、连续空行、真实尾空格、中文及组合字、属性、受保护文字选择性擦除、后续输出、选择/滚动锚点及历史限额。
- Appearance 定向 15 通过：长行与字号、重排 padding/光标、超长逻辑行、备用屏/历史限额、pending wrap、查找、行距导航/撤销及语法/保存。
- Editor highlights 全套 5 通过，CTest editor_highlights 1/1：暗浅三态、空行/滚动/缩放、多编辑器焦点，真实 Return/Shift+Return/数字键盘 Enter 导航后选区和正文保护。
- 主线程独立 probe：终端 7 通过、编辑器 4 通过、高级搜索 8 通过。新增 protected_cell 回归和查找 Enter 正文保护均通过。

红证据保留在 test-results/0.1.40/worker-red、editor-highlights、editor-trace 及主线程 main-* 文件。最终报告与实际暗浅截图在 terminal-final、appearance-final 和 editor-highlights-final。源码 JSON/ZIP 快照、目录 inventory、逐文件 payload、双 PE 图标与隔离启动证据另存于本版 test-results，不写入用户配置或已有发布包。

用户原 PM2 会话尚无完整 VT 录制；本轮使用独立复现覆盖已证实缺陷，不宣称验证了所有现场原因。未实测 macOS/Linux 构建、原生桌面合成及真实服务器网络时序。既有 dist、0.1.39 全部证据和批准参考由 1291 文件保护基线逐一校验。
