# RabbitShell 0.1.28 验证记录

2026-11-10。本版落实资源树文件、目录、多选、空白右键菜单的紧凑矢量图标及 HTML 预览的尺寸/留白；移除单文件“打开文件”项，保留双击。仅资源菜单自绘 action 行，继续使用真实 QMenu/QAction 的输入、checked、焦点、可访问及原回调目标，其他菜单未改成新框架。精确参数见《右键菜单》。

受影响 CTest 6/6 套通过：menus 27、rounded_surfaces 22、diagnostics 6、file_transfers 29、appearance 31、workspace 29 passed，均 0 failed。最终调整菜单顺序后，Release 重建并仅针对 menus/workspace/file_transfers 再次验证 3/3 套通过，未重复无关套件。file_transfers 的 10 个专用私有 SFTP 夹具用例按环境条件 skipped；本轮不重跑无关协议矩阵，不将其写为本轮通过。菜单精确样式专项 15 passed/0 failed/0 skipped；DPR 1.5 专项 13 passed/0 failed/0 skipped，最终顺序的 z2/超高滚动专项再获 5 passed/0 failed/0 skipped。

真实 widget 截图覆盖两主题、四种菜单、z2，以及单列超高菜单。测试核对实际 action/icon/text/check 几何与边框、hover 像素，左右图标和勾相互独立；真实鼠标勾选、菜单 Enter、Home/Down/End/Escape 与焦点恢复通过。超高菜单以鼠标悬停上下原生箭头实际滚动，键盘可到达全部启用项，保持单列。主审已目视接受 `test-results/0.1.28/exact-menu/` 的精确样式；最终分组顺序及其 DPR 1.5 截图位于 `final-menu/` 与 `final-menu/dpr150/`。行菜单先下载/上传，剪切/复制/粘贴居中，删除独立成组、属性置底；空白菜单不显示剪切/复制，原操作目标保持。

初次集成曾把资源树 itemDoubleClicked 改为 itemActivated，inline 新建 Return 因此额外请求了其他 current 文件，旧 workspace 测试明确失败。已撤回该扩展并恢复原 itemDoubleClicked，没有放松既有 jobs.size 断言；最终同一测试及真实双击打开回归通过。初始失败保留于 `ctest-integrated-initial.txt`，最终结果为 `ctest-final-combined.txt`。早期截图/报告由 `exact-menu/` 与最终结果取代，不沿用早期原生分列结论。

主程序诊断独立子进程覆盖合成 SEH、terminate、正常退出、handler 恢复、16 槽轮换、槽被占用及无效路径；侧车临时报告保留在 `diagnostics-sidecar/report-final.txt`。portable 阶段核验 Kernel32-only 静态导入、三项私有启动烟测及 ChildExit 隔离路径。源码与包内文档在 pin 前冻结；源码清单/ZIP、目录清单、generated payload 和 PE 各文件 size/SHA 及最终烟测证据记录于包外交付报告，避免包内文档自引用哈希。

全部测试使用私有配置、合成数据与 Qt offscreen，不操作真实服务器、用户凭据/信任、真实系统剪贴板或可见桌面，不代表 Explorer 或混合 DPI 桌面实机验收。此前闪退根因未知，异常捕获是最佳努力；详细边界见《0.1.28 诊断说明》。本轮也不宣称修复此前未完整复现的 Vim 覆盖现象。

0.1.27 文件操作边界继续有效：撤销为会话内最近 100 批元数据，无重启恢复/redo；删除和覆盖阻断整批撤销，非空上传目录不能自动撤销；不创建远端恢复文件或覆盖备份。主动保存/上传允许短期 `.part`，正常成功/取消清理，异常退出或断线可能残留且不自动远端扫描删除。长上传期间其他路径的变更也可能提示 busy 后稍后重试，草稿保留，列表/读取/终端不受该变更锁阻塞。
