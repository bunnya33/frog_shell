# RabbitShell 0.1.25 验证记录

任务日期：2026-11-10。Windows x64、Qt 6.8.3、Release。

## 实际变更

右键菜单按语义分组，短菜单平铺，动态条件省略空组，不产生首尾/连续分隔线。文件目录下载合并为「下载目录文件」，空白菜单只作用于已提交当前目录；原行选择可保留，但不带旧选择的打开、下载、重命名、属性或删除。草稿/无有效绝对路径 metadata 拒绝操作；普通 POSIX 特殊名称不受收藏路径规范误伤。菜单目标捕获值快照，重命名确认后核对原 session/代次，F2 仅右侧列提示，不重复注册快捷键。连接/收藏、终端正文与管理、编辑和工作区页签、端口、服务及传输均保留原条件、确认和 callback。完整分组见《右键菜单》。

同轮修正圆角容器描边缺失。公共 roundedsurface 共用外轮廓与实际半线宽内缩，在已有语义容器上使用单个鼠标穿透描边层保护四角，不给普通布局添加卡片。服务卡删除与 QSS 半径不一致的整窗 mask；进程卡保留连续表头/表体及顶部圆角，替换固定 0.5 内缩，不新增内部线。card/transfer/find、收藏、连接/端口表格、状态摘要、字体预览、连接提示及路径框同步覆盖。普通 QSS 输入框/按钮沿用原圆角。具体审计表见《圆角边框》。

QMenu 仍由 Qt 处理输入、子菜单、键盘和关闭，客户端绘制一致的圆角背景与完整边框。Windows 仅对 popup 设置公共 DWM corner preference，防止系统额外半径裁去客户端弧线；保留主窗/对话框标题栏及高对比度系统回退。StyledCombo 保留 Qt 自有 popup/selection，透明属性仅设置在确认的 Qt::Popup，不使主窗透明。旧系统 fallback region 含抗锯齿包络，随 resize/zoom 更新，后续成功清除，无周期 timer 或位图缓存。

## 最终检查

`build-final.txt` 成功构建 RabbitShell 和以下相关测试，最终应用版本为 0.1.25。`ctest-final.txt` 为 **13/13 全通过**：

| 目标 | pass | fail | skip |
|---|---:|---:|---:|
| menus | 14 | 0 | 0 |
| rounded_surfaces | 22 | 0 | 0 |
| appearance | 31 | 0 | 0 |
| services | 13 | 0 | 6 |
| ports | 9 | 0 | 2 |
| terminal | 18 | 0 | 0 |
| file_transfers | 25 | 0 | 3 |
| workspace | 23 | 0 | 0 |
| bookmarks | 10 | 0 | 1 |
| credentials | 11 | 0 | 6 |
| editor_groups | 34 | 0 | 0 |
| processes | 8 | 0 | 0 |
| controls | 6 | 0 | 0 |
| 合计 | 224 | 0 | 18 |

计数含 QtTest init/cleanup。18 skip 为未启用的 6 个服务、2 个端口、3 个文件、1 个收藏、5 个凭据/PTY loopback 及 1 个系统凭据库 opt-in。它们未在本轮执行，不计为通过；本次界面改动未重复协议/压力矩阵，也未接真实服务器或读真实凭据/信任记录。逐项 skip 原文在 `test-summary.json`。

14 项菜单检查实际生产 QMenu、动作顺序、动态空组、空白与旧选择隔离、目录单下载入口、特殊名称、F2 空 shortcut、目标快照、共享收藏菜单、端口取消、服务模板和传输状态。服务既有模板断言仅跳过 QAction::isSeparator，其余动作限制不减。终端/编辑/工作区/收藏/管理凭据和原 .23 文件树可访问缓存用例继续通过，未禁用辅助功能或重置业务模型。

圆角 22 pass 覆盖暗浅 × zoom -1/0/+1 × render DPR 1/1.5/2 的 18 行、受控 fallback 生命周期、真实进程/服务卡及菜单/子菜单/combo，再加 init/cleanup。四个弧线与直边接合点逐段像素探针，最小物理线宽的抗锯齿容差按宽度调整；不把 0.83 物理像素细线要求成不透明整像素。fallback 对比未裁切图像的已绘像素，验证 resize/zoom 更新和清 mask，主窗不被设置透明。现有 controls 的 popup 位置、滚动、Esc、Enter、鼠标选择和激活语义均通过。

隐藏 Windows 后端独立报告 `rounded-windows-final.txt` 为 **4/0/0**，与 CTest 合计分列。两主题实际 HWND 的 DWM setter/getter 返回 S_OK，菜单回读 preference=1；combo setter S_OK。所有测试顶层窗 WA_DontShowOnScreen=true，实际客户端截图和 JSON 在 `rounded-windows/`。这是 Windows 后端隐藏 HWND 的 Qt grab，**不是 CreateDesktop 或 DWM 合成桌面截图**。未操作可见用户桌面，未宣称真实混合 DPI/多屏、旧 Windows 或高对比度实机验收。

## 保留的早期记录与视觉检查

`menus-initial.txt` 为 6 pass/8 fail：fixture 未清理 setup 的 terminal resize、context event 未送 viewport、传输项未放入生产所需批次父行。`menus-second.txt` 的异常退出为测试栈 QAction 被终端 parent 提前释放。修正 fixture 后 `menus-third.txt` 14/0，随后最终 CTest 再次通过；未因此修改传输状态机或放过未知 job。

`rounded-initial.txt` 最小 zoom/DPR1 行的固定像素容差失败已保留；`rounded-green.txt` 和早期 `rounded-windows.txt` 虽测试计数通过，但含重构 QSS 后的 QString::arg missing 警告，**不作为最终无警告报告**。已修正 section %4 及对应四参，最终 `rounded_surfaces-final.txt`、`rounded-windows-final.txt` 和全部选定 CTest 无该警告；最终检查的计数和无警告断言见 `test-summary.json`。

主审逐张只读查看暗/浅文件、管理、收藏、服务、端口、终端、传输菜单及 cards、缩放 combo、隐藏 Windows menu/combo 客户端图，分组、F2 对齐和连续圆角描边通过。源通用规范补充动态分组、快捷键列及统一圆角/描边/裁切规则。截图均使用私有数据和真实中文字体。

## 冻结与交付

开工完整 180 文件源快照/ZIP 和 .12–.24 共 26 目标/969 文件 inventory 保存于 `test-results/0.1.25/`。源码/文档冻结后生成独立 `dist/RabbitShell-0.1.25`，pin approved inventory，再显式 `-Version 0.1.25` 生成 `dist/RabbitShell-0.1.25-portable.exe`。旧包不回写，不执行 git commit/reset/clean。

目录逐项核对、generated manifest/PE resource 解包比对、最小 PATH/中文空格/仅 EXE/私有配置/offscreen 的 version/manager/demo 三项烟测，以及源码差异清单、旧包核对和最终 SHA 在 final-report 中记录，不在冻结文档预写包装通过。
