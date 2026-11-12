# RabbitShell 0.1.23 文件树辅助功能缓存修复

任务日期：2026-11-09。**已确认并修复可导致辅助功能遍历重复provider的缓存缺陷；用户点击MD后完整的高CPU/数GB内存增长过程尚未在隔离环境复现，不宣称所有未响应已根治。**

## 证据

用户 .22 日志显示FilePress/Hit/BasePress/PressEnd已返回，随后dispatch=0/none、无release且GUI心跳停止；MD可能只是单击，不能推断已开始read/editor。仅获准复制结构化JSONL，开工源快照169文件和旧 .12–.22 共22目标805文件均已保存。

在本次故障排查范围内，对已核对image/启动时间/HWND的实际GUI线程只做有界只读栈采样，不生成dump、不输出locals/原始栈字节/用户内容/命令行，不注入、不改输入法或终止用户进程。初始3样本均在NtUserMsgWaitForMultipleObjectsEx等待，不能当作高CPU根因。后续先确认0.400秒内GUI用户态CPU增加0.375秒、内核0.03125秒，再取2样本：UIAutomationCore/COM→qwindows+0x7ccc7→Qt6Widgets/Qt6Gui；暂停约33.5/5.2ms后均恢复。第一批最长45.8ms，未超过0.5秒上限。只输出模块basename+offset，证据在`test-results/0.1.23/live-gui-stack*.json`，后续不再采真实进程。

Qt6.8.3源码确认：QAccessibleTree::child按visible row×columns缓存；indexOfChild却根据同一子项的persistent model index计算当前行。层次、排序、过滤变化后旧缓存可能不再互逆；UIA NextSibling用indexOfChild+1再取child，能重复同一provider。新测试prime缓存后6类变更，offscreen与隐藏Windows均出现互逆/失效项红灯；显式WM_GETOBJECT激活Windows UIA后collapse仍失败，排除仅offscreen未激活造成的假阳性。遍历每个起点的有界NextSibling规则测试明确重复provider（`navigation-cycle-red2.txt`）。这证明具体缓存循环，未把有限遍历测试冒充用户8GB现场复现。

## 最小修复

仅FileView维护辅助缓存：模型结构/布局前后、展开/收起时，通过公共`QAccessibleTableInterface::modelChange(ModelReset)`失效旧子接口；render与filter变更前显式调用，兼容树信号屏蔽和setHidden无模型结构信号的情况。原Qt延迟layout、模型与平台通知机制继续使用；不在通用绘制/accessible cell回调内增加清理，不升级或改写Qt DLL。

这里的ModelReset只发给辅助表接口，不调用业务模型reset、不重建树，不改变selection、展开状态、草稿、文档或PTY。无新timer/worker/远端查询；辅助功能保持开启。FileView析构先断开模型和自身回调，避免QTreeWidget基类销毁model时回调已析构的preview成员。实验harness曾暴露该析构访问异常，最终生产清理及现有销毁回归已通过；它不同于用户的Hang。

## 最终验证

Release / Windows x64 / Qt6.8.3，私有配置、offscreen/WA_DontShowOnScreen隐藏Windows及loopback SFTP。CTest **5/5**；含init/cleanup共 **72 passed / 0 failed / 5 skipped**：diagnostics5、native_drag10（2skip）、file_transfers25（3skip）、core9、workspace23。三个upload fixture skip由独立SFTP **5/0/0** 覆盖；两个native SFTP fixture本轮未跑，常规COM拖放回归通过。未扩展SSH/launcher压力矩阵。

- 缓存契约6类变更、重复provider遍历修复后全绿；offscreen9/0/0、Windows9/0/0。新增旧ID用例16轮prime→变更→枚举，每轮旧ID都已移除，新child仍互逆，活跃子项至多18，未持续保留旧ID；隐藏Windows合计10/0/0。此为有界生命周期断言，不是完整内存benchmark。
- 用户最新操作：prime缓存→展开/收起/排序→真实QTest鼠标单击MD，itemPressed/itemClicked均返回且selection正确、未单击就read；再双击产生read Job、模拟短Markdown结果，Editor建成可关闭，新缓存仍互逆。与旧ID用例隐藏Windows4/0/0，亦纳入最终file_transfers。
- 原上传、冲突、深层刷新、确认期间list reply、删除、选择、拖放、草稿及工作区生命周期回归保持。loopback实际上传→刷新→删除链通过，无远端exec、无遗留.part。
- 外部COM实验保留在报告目录，曾取到外层Win32节点/旧provider等，不作为循环红灯或正式交付通过数；没有用隐藏COM的FindAll结果宣称完整原生Hang复现。正式证据为活跃现场只读栈、可访问契约/NextSibling循环红绿及隐藏Windows旧ID/MD操作测试。

证据目录`test-results/0.1.23/`：`accessibility-contract-red.txt`、`accessibility-active-windows-red.txt`、`navigation-cycle-red2.txt`、`cache-offscreen-green.txt`、`cache-windows-green.txt`、`cache-final-windows.txt`、`md-and-ids-windows-final.txt`、`ctest-final.txt`、`test-summary.json`及`sftp/`。辅助功能测试是合成树，不读取真实用户文档；没有真实SFTP/凭据/信任读取或可见桌面自动化。真实MD会话全过程、混合DPI/实际辅助客户端组合及非Windows构建仍未验收。

## 交付

新目录`dist/RabbitShell-0.1.23`与单EXE`dist/RabbitShell-0.1.23-portable.exe`；旧22目标805文件按开工完整inventory核对。最终app核对，文档冻结同步目录→pin approved inventory→显式`-Version 0.1.23`打portable。静态PE解包逐项核对及仅EXE、中文空格、最小PATH、私有配置、offscreen的version/manager/demo三项烟测，实际hash/结果见`final-report.json`；源变更见`source-files.json`，本文不预写包装通过。
