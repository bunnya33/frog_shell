# RabbitShell 0.1.30 验证记录

2026-11-10。本版调整资源操作冲突文案、保留两者命名、粘贴可见性和显示开关入口，使同父目录内部拖动成为无反馈干扰的 no-op，并增强按钮悬停/按下反馈。主审已接受实现方向、真实 Qt 暗浅截图及专项，随后授权最终版本、文档、冻结和打包。没有新增服务器辅助文件或恢复机制，保留既有传输、身份、编辑器、撤销和原生拖出复制保护。

冲突标题区分复制/移动/上传，按钮统一“取消”，默认和 Esc 取消。复制仍经过既有本机暂存/传输，文案不再误称“取消上传”；复制/上传允许按原规则替换普通文件、保留两者、应用同批冲突，移动只允许保留两者或取消。统一 basename helper 用于 Session 的上传/下载/移动和 Windows 虚拟导出重名显示：`install (1).sh`、`archive (1).tar.gz`，目录 `folder.name (1)`、`.env (1)`、`file (1)`；候选占用后递增，路径锁也匹配扩展名前编号，不锁无关同级文件。原有 no-replace/atomicreplace、取消和 undo 规则不变。

资源右键只有在只读前置检查通过时显示粘贴；纯文本、网页、缺失/链接、超限路径、伪造/失效 token、准备中/失败复制、跨服务器 cut 均隐藏。实际粘贴复用同一检查，菜单打开期间随私有 QClipboard dataChanged 和连接状态更新，连接生命周期随菜单结束，目标保持打开时捕获。菜单截图/矩阵只比较可见动作和 Qt 未折叠分隔线，保持原分组与回调断言。

显示隐藏文件和修改时间列从四种右键移至资源导航栏最右侧 More 图标。弹出时 aboutToShow 同步两项真实 QAction checked，左侧眼睛/日历图标、右侧勾选沿用紧凑菜单风格；保留原 QSettings、文件过滤、修改时间列显隐及宽度。暗浅 z0/z2、真实鼠标/Enter、隐藏切换和宽度137往返验证通过。

同父目录内部落点是合法 no-op，Qt 路由返回 Move、释放接受但不发业务移动信号，不弹确认、不增加任务或历史，也不触发远端写入；从有效目标移回 no-op 会清除 uploadPreview 和提示文字。混选确认及任务只包含需要移动的项。自身/后代仍非法，原身份/代次、父导航、链接/草稿、跨服务器、确认取消和按批撤销保留。测试通过 QWindowSystemInterface 实际窗口路由，非只直送合法目标。

按钮集中主题覆盖 toolbar icon、tool text、普通文字、primary、activity 五类，暗浅均验证 normal/hover/pressed/released/checked/checked-hover/focus/disabled。使用实际 Qt 鼠标、Space、tooltip 和像素对比，释放恢复 hover，checked/活动标记保留，禁用不触发，尺寸不变。沿用 QSS 和 Qt 状态，不新增常驻 timer、图像缓存或按钮对象。真实状态图在包外 `test-results/0.1.30/focused/button-feedback-{dark,light}.png`；并非视频帧率或全场景性能验收。

Release 受影响 CTest 七套 7/7：core17、menus32、native_drag13、file_transfers43、appearance37、workspace29、rounded_surfaces22 passed，均0 failed；file_transfers12和native_drag2项需专用fixture，普通套件按条件 skipped。专项为 core9/0、menu26/0、transfer6/0、button4/0（含 init/cleanup）。最终截图辅助仅补充 Qt 折叠分隔线过滤和 no-op 实装图，相关菜单/拖动复测通过。

私有 loopback SFTP 四个业务用例共6 passed/0 failed/0 skipped（含 init/cleanup）：实际 copy/upload、download、move 对 install.sh、archive.tar.gz、.env、无扩展名及点号目录生成已避让 `(1)` 的 `(2)` 候选；原目标和已占用候选内容均保持。另保留真实上传取消/竞争/类型冲突/不覆盖、内部父/子/跨目录移动与撤销、clipboard复制和剪切回归。审计 remote exec0、无.part或recovery残留。没有扩大无关协议矩阵或接入真实服务器。

开工220源项逐条匹配冻结0.1.29，旧.12–.29共36目标/1418文件按完整路径/size/SHA保留。最终版本Release、源清单/ZIP、approved/generated/目录/PE逐文件核验及目录/portable的version、manager、demo三项烟测均在包外 final-report 记录，文档在pin前冻结，避免自引用哈希。

全部验证使用私有数据、offscreen剪贴板/Qt和本机loopback，不接触真实配置/凭据/信任/剪贴板或可见桌面；不是Explorer或混合DPI实机验收。原会话内100批undo、删除/覆盖barrier、非空新上传目录不可自动撤销、长上传期间mutation busy和草稿保留边界不变。主动save/upload仍使用已授权短期.part，异常退出/断线可能残留，不自动远端扫描删除。此前闪退和未完整复现Vim现象不宣称修复；.29参考行距和设置滚轮规则保留。
