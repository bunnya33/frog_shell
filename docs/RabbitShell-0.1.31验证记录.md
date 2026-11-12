# RabbitShell 0.1.31 验证记录

2026-11-10。本版仅修正标准按钮焦点提示：鼠标 hover/press/release 和 More 菜单返回不显示外围 accent 框或原生内框，保留 .30 背景反馈、checked 底色/图标和 activity 左条。Tab/Backtab/Shortcut/Space 保留真实焦点与单层键盘提示，主按钮使用对比边框。业务菜单、传输/拖动和远端操作不变。

共享过滤器只处理 QPushButton/QToolButton 的相关输入/FocusIn，其他事件先按枚举返回。keyboardFocus 模式变化才局部 repolish；QSS outline:0 消除原生重复框。不改焦点策略、不主动 clearFocus，不增加每按钮 timer、OS hook、无界缓存或逐事件全应用重绘。

最终 buttonFeedback 暗浅专项4/0，实际鼠标点击和释放后保留焦点；键盘转鼠标后整图与初次 hover 相等，覆盖原生灰内框，且实际 Tab/Backtab/快捷键/Space 保留键盘提示。More 的暗浅 z0/z2 经真实 popup 路径验证鼠标打开后 Esc/Enter/点选关闭无框，键盘打开后关闭保留提示；More/菜单专项7/0。最终 appearance37、menus32 全部通过，CTest2/2，无 skip。旧专项通过但图片暴露的内层框保留在包外 native-inner-before，最终以 button-final 图为准。

开工222源项匹配固定 .30，旧38目标/1512文件全哈希保全。主审已接受最终图和回归并授权冻结打包；源清单/ZIP、approved/generated/目录/PE逐项核验及目录/portable三项私有启动烟测记于包外 final-report，不在包内自引用哈希。测试仅私有 offscreen，不触用户真实配置/凭据/剪贴板、可见桌面或服务器，未跑无关SFTP/协议矩阵，不宣称修复此前闪退/Vim问题。
