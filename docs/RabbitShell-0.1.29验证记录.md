# RabbitShell 0.1.29 验证记录

2026-11-10。本版修复资源树内部移动的入口路由与父目录目标、资源菜单粘贴启用状态、设置悬停滚轮误改值，并按最终参考图应用编辑器行距。保留 0.1.28 的菜单样式/顺序、双击打开和本机诊断，没有新增远端 helper、恢复文件或覆盖备份。

内部移动使用独立父目录目标解析，导航行不再套用上传禁用规则；本地上传和剪贴板粘贴仍不可落到 `..`。可信有效来源的 DragEnter 建立 Qt viewport 路由，hover/drop 再检查实际目标。Qt 6.8.3 QApplication 只有 Enter 接受后才记录 currentTarget；此前无效入口导致后续合法目录的事件被丢弃。QWindowSystemInterface 的一次 handleDrag 还会发送当前位置 Move，所以无效入口初次仍是 Ignore、随后合法目标才是 Move。身份/代次、失效 token、自身/后代、同目录无操作、链接/草稿和确认取消限制保持。

修复前 `drag-before-window-routing.txt` 为 2 passed/5 failed，修复后 `drag-after-final.txt` 为 14 passed/0 failed/0 skipped，包含 source/blank/link/draft 入口、子目录、deep Windows bridge、父导航、来源失效及两主题真实 widget 截图。测试通过平台→QWindow→QApplication→实际视图分发，不只向合法目标直接 sendEvent；这不是 Explorer 实机鼠标验收。

粘贴菜单与实际 pasteFiles 共用只读前置检查；纯文本、网页、无效或链接路径、伪造/过期令牌、准备中/失败复制、跨服务器剪切置灰。合格本地普通文件/目录或有效同服务器剪切启用，菜单打开期间随内容/状态更新，连接以菜单生命周期为界。文本启用的修复前失败保留，`clipboard-after.txt` 为 3 passed/0 failed；该单个业务用例实际打开菜单并逐项替换私有剪贴板内容，没有发 SSH 或读取文件正文。

设置覆盖三类字号及字体/字重/缩放的闭合下拉框。只有点击启用且聚焦后滚轮修改，未启用时滚动父 QScrollArea；标签、空白或真实失焦退出，键盘/上下按钮/展开 popup 保留。集成曾暴露 spin shell 按下传播到父页面被误判外部点击，以及 combo 转入自己的 popup 时第二次 OtherFocusReason 清除状态。现以局部 NoMousePropagation 和实际 popup/focus 归属修正，原点击、真实内部 lineEdit、窗口 wheel、页面滚动、popup 收起继续滚轮的断言均保留。初始/诊断失败继续保存在包外，`settings-wheel-own-popup-final.txt` 为 3 passed/0 failed。

编辑器最终按截图采用字体像素 `19/14` 比例，默认 Cascadia Code NF 14px/500 对应 19px 行高；随字号/界面倍率取整且不低于自然字形高度，末块保留文档 margin。不是 +2/+4 提案，不改变基础字号或终端。`line-spacing-implemented/implemented-ascii.png`、`implemented-chinese.png` 和 `implemented-metrics.json` 是真实 CodeEdit；中英文 baseline steps 全为 19，普通块 19、末块 23。三个专项验证基线/命中、惰性布局、翻页/查找、字体/倍率反复变化、草稿/选区/文档/撤销重做栈保持。布局没有每 block 新增缓存，文档遵循 Qt 所有权，不宣称全面内存泄漏排查。

最终受影响 CTest 六套全部通过：appearance 35、workspace 29、editor_groups 34、menus 28、file_transfers 39、native_drag 13 passed，均 0 failed。普通 CTest 的 file_transfers 11 个、native_drag 2 个 fixture 用例按条件 skipped；本轮单独运行三个受影响私有 SFTP 业务用例得到 5 passed/0 failed/0 skipped（含 init/cleanup），验证父级/子级/其他目录移动、取消、多根文件/目录批次及撤销、原 move/占位保护和 clipboard 传输。远端 exec 0、无上传临时残留且无 recovery 文件；没有重复无关协议矩阵。

测试构建的 MIME 格式名类型歧义已显式转换修正；首次 SFTP runner 缺少既有 `.tools/test-python` 的 PYTHONPATH，补环境后通过，未改 fixture 或放松断言。全部中间失败保留，不把它们当最终结果。早期行距 PNG 是包外提案，以实装截图和度量取代。最终源码、目录文档在 pin 前冻结，清单/ZIP 与包各项 size/SHA、静态检查及目录/portable 三项启动烟测记录于包外 final-report，避免文档自引用哈希。

开工 219 源项与接受的 0.1.28 完全一致，旧 .12–.28 共 34 目标、1325 文件保留。全部验证使用私有配置、offscreen 剪贴板及 loopback，不接触真实服务器、用户配置/凭据/信任、系统真实剪贴板或可见桌面；并非 Explorer/混合 DPI 桌面验收。此前闪退根因及未完整复现的 Vim 现象仍未知，不宣称本版修复。

资源操作原边界保持：会话内最近 100 批撤销，无重启恢复/redo；删除和覆盖阻断整批撤销，非空新上传目录不能自动撤销。不创建远端持久恢复文件。主动保存/上传使用已授权短期 `.part`，正常成功/取消清理，异常退出或断线可能残留，不自动远端扫描删除。长上传期间其他路径变更也可能 busy 后重试，草稿保留；读取、列表和终端不受该变更锁阻塞。
