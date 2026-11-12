# RabbitShell 0.1.24 验证记录

任务日期：2026-11-10。Windows x64、Qt 6.8.3、Release。

## 实际变更

新增独立 DirectoryBookmarkStore，以 profile.id 分域，QSaveFile 原子保存稳定条目 ID、绝对 POSIX 路径、别名及顺序。资源树目录或当前目录空白菜单可添加；下方独立收藏卡片可调高、调列宽、内部平级拖动排序，右键别名、移除及分别打开目录/终端。连接管理由五列表改为五列树表，目录子项与服务器动作分离，按 ID 增量复用并恢复展开、选中、当前项和滚动。

收藏打开先验证目录，连接中保留最新意图；失败保留入口并显示原因。Both/Directory 成功后切资源管理，Both/Terminal 切终端页并聚焦新终端。原 PTY 不变，不向忙 shell 注入 cd；继续沿用既有新 channel 的 shellQuote。手动导航/刷新、取消/断线/关闭、身份更改或移除入口会撤销意图。提交前绑定 job/session，覆盖队列满时 submit 同步 emit failed 的重入。详细数据边界与操作规则见《目录收藏》。设置重置保留收藏，旧工作区不能为已删除连接创建新入口。

保留 0.1.23 FileView 可访问缓存修复及析构 cleanup，提取为共享 SafeTreeWidget，覆盖新增树。显式局部刷新完成 doItemsLayout 后清缓存；layoutChanged 保留即时失效，再合并一次有上下文的非周期延后布局/失效，避免在模型信号中递归布局。业务模型、选择、文档和 PTY 均不 reset，也未禁用辅助功能。

## 验证结果

最终构建 `build-final.txt` 成功，含最新 UIlib 链接的 RabbitShell.exe。8 项相关 CTest 全通过：

| 目标 | pass | fail | skip |
|---|---:|---:|---:|
| bookmarks | 10 | 0 | 1 |
| core | 9 | 0 | 0 |
| workspace | 23 | 0 | 0 |
| file_transfers | 25 | 0 | 3 |
| credentials | 11 | 0 | 6 |
| editor_groups | 34 | 0 | 0 |
| appearance | 31 | 0 | 0 |
| controls | 6 | 0 | 0 |
| 合计 | 149 | 0 | 10 |

计数含各 QtTest 可执行文件的 init/cleanup 行。CTest 不提供网络 fixture，故跳过 1 个收藏 loopback、3 个文件 loopback；它们已另外实际执行。credentials 的 6 skip 为系统原生凭据库 opt-in 和 5 个旧凭据/PTY专用 fixture 用例，本轮未运行，不记作通过。未接真实服务器、读取真实密码或信任文件，也未运行无关压力矩阵。

收藏专项覆盖 CRUD/空别名/顺序/重载/多连接隔离、原子失败不提前改变内存、坏文件保留、容量和 UTF-16 非 BMP、拒控制字符/`..`；真实右键非根目录添加与去重，Qt model MIME drop 走生产重排 controller 并拒外部列表/Copy/嵌套；管理器真实点击+双击、子项菜单/F2隔离、重排及删前项后的展开/选中保态；连接中 latest、空目录成功、失败/取消/手动导航/迟到/移除/身份变化、队列满和已有终端不变；侧栏和底部页签可见性及新终端焦点。

`bookmark-sftp/bookmark-sftp.txt` 为 **3/0/0**：真实私有 loopback SSH/SFTP，使用明确指定、无启动文件、私有 HOME 的本地 MSYS bash，并把 SFTP 根映射到 fixture 磁盘。PTY 新通道的实际 cd/pwd 确认中文、空格、单引号和字面 `$()` 路径；新开空目录终端后旧通道 cwd 仍不变，不存在目录不新建终端。实际输出在 `bookmark-sftp/pty-cwd.txt`；不是字符串比对伪造的 cwd，也不是远端生产服务器验收。`file-sftp/sftp-integration.txt` 为 **5/0/0**：上传、刷新、冲突等待/销毁、完成后删除等既有隔离链路，无远端 exec、无遗留 .part。两份 loopback 总数单列，不重复并入上述 CTest 合计。

原 .23 文件树的 6 个 expand/collapse/sort/filter/refresh/delete 契约行、旧 ID 退役、有限 NextSibling 和 MD 点击/模拟 read/editor-close 共 **9 行全部 PASS**，包含于 file_transfers 的 25 pass。新增 manager/收藏树多轮展开、排序诊断、过滤、重排、增删后索引互逆及有限 sibling 也通过。

保留的红灯 `bookmarks-diagnose.txt`/`bookmarks-cache-green.txt` 定位到 **favorite-sort 后、favorite-refresh 前**；后一个文件虽命名带 green，实际总数仍 9 pass/1 fail/1 skip，不作为最终通过报告。显式 sortItems 后缓存项的 indexOfChild 与位置不一致；即时模型通知到达时视图行布局仍可能延后。合并延后布局/再次失效后，`bookmarks-final.txt` 及最终 CTest 全绿。收藏列排序不是产品入口，产品顺序由内部重排/store 决定；该诊断覆盖共享 Qt 缓存路径，未因此增加新排序功能。

暗浅实际 Qt 截图 `bookmarks-workspace-{dark,light}.png`、`bookmarks-manager-{dark,light}.png` 已人工查看：中文、目录图标、卡片边界和路径列正常。均为私有 offscreen 数据，未操作可见桌面。本轮未声称真实 Windows UIA 外部客户端、真实生产 SFTP、混合 DPI 实机或非 Windows 编译已验收，也不扩大 .23 用户历史 Hang 的根治结论。

## 冻结与交付

开工完整 source-before 170 文件及 ZIP；旧 .12–.23 的 24 目标/886 文件完整路径、size、SHA256 inventory 逐项核对不变。最终源码清单、源码冻结摘要、测试汇总及包装结果位于 `test-results/0.1.24/`，主审只读独立复核另列于 final-report。

独立目录 `dist/RabbitShell-0.1.24` 和单 EXE `dist/RabbitShell-0.1.24-portable.exe`。源和本文冻结后同步目录、pin approved inventory，再显式 `-Version 0.1.24` 生成 payload；包内文档不在 pin 后改写。逐文件静态/PE 解包核对、目录 verify-package、仅 EXE/中文空格/最小 PATH/私有设置/offscreen 的 version/manager/demo 三项 portable 烟测结果及 app/portable/payload SHA 见 final-report，不在本文预写包装通过。
