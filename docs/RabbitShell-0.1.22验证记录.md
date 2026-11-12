# RabbitShell 0.1.22 未响应诊断版

任务日期：2026-11-09。**用户 Hang 尚未复现，根因未定位、未修复。** 本版补齐日志盲区，保留目录展开、刷新和删除算法及既有交互。

## 真实证据与解释

用户两次澄清：点击文件夹左侧「小三角」；点击后即卡住，「还没展开」。获准复制的 .21 白名单 JSONL 已保存在 `test-results/0.1.22/user-diagnostic.jsonl`，SHA-256 `9249276b3260d1ed3af4daf9b7f001bf6ded22abbb0d2a9605d2962b98842902`。未读取其他用户应用/凭据/信任文件，也未抓取或终止真实进程。

旧 job3 的3条结果在21.719秒返回、RenderEnd在21.720秒；最后心跳22.498秒，之后UiStall持续，151.306秒又有正常心跳。job3可能属于前一次展开，不能证明这次失败点击已到itemExpanded/ListRequest，更不能证明根因在render。主审提供的高CPU及约6.76GB WorkingSet/7.86GB Private说明本地资源失控；没有用户调用栈，不能宣称native crash、永久死锁或某个Qt函数已被定位。

## 新增诊断

- 生产主程序使用 `DiagnosticApplication`，经公共 `QApplication::notify` 栈作用域，仅在内存保存当前 QEvent 数值、固定白名单接收/父类型、嵌套深度及开始单调时间。嵌套完成恢复外层，不保留对象指针，不逐事件编码/落盘。未知具体类映射到白名单父类/other，无 objectName、动态属性、原始类名或用户文字。
- 后台UiStall追加 `dispatchEvent/dispatchClass/dispatchOwner/dispatchDepth/dispatchAgeMs`，同时保留lastUi；无活动事件时depth/age为0。常见QEvent值：Timer1、MouseButtonPress2、MouseButtonRelease3、Paint12、MetaCall43、ToolTip110。当前事件说明当时仍在该dispatch内，不等同于精确native调用栈或根因。
- FilePress、FileHit、FileBasePress、FileRelease、FileExpanded的Begin/End，区分本地itemAt命中、Qt基类press与expanded/list提交之前；MonitorUi、ProcessSnapshot、TerminalFeed、TerminalResize的Begin/End覆盖后续UI处理。只记录数量/按钮数值，不记录监控文本、终端输入输出或错误原文。
- 延续 .21：数据目录优先，稳定本地logs；512条/256KiB队列，队列+writer batch日志负载至多约512KiB，3份各2MiB文件；锁内仅有界内存移动，约1秒离锁批量写盘/flush，正常5秒heartbeat，UI每500ms仅更新内存，停顿≥2秒后台记录、重复至少间隔5秒。无额外网络轮询/远端exec，无全局qDebug handler。磁盘故障/强制退出仍可能丢尾部。

## 本轮验证

Release / Windows x64 / Qt6.8.3。CTest **7/7**；含init/cleanup合计 **89 passed / 0 failed / 5 skipped**：diagnostics5、native_drag10（2 skip）、file_transfers16（3 skip）、core9、terminal18、processes8、workspace23。三个upload fixture skip在独立loopback实际SFTP **5/0/0** 中覆盖；两个native SFTP fixture本轮未运行，常规COM/拖放回归通过。不重复无关SSH/launcher或压力矩阵。

- `arrowClickBeforeList` 用真实QTest鼠标按下/释放命中小三角（不是名称/双击/setExpanded）：18条根→8条子目录→3条infra记录；下一个独立点击展开相邻docs及嵌套目录，靠下scroll、收起再开、大小降序，断言expanded与对应队列路径。offscreen和隐藏Windows均通过，未复现挂起。
- `delayedExpandRefreshAndTooltip` 交错延迟tooltip、无障碍接口查询、2秒monitor及进程表更新；生产installWindowStyle+隐藏Windows同样通过。外部watchdog仅采样自己创建的合成子进程，12秒/512MiB Private上限；生产WindowStyle两用例峰值约136.5/136.1MiB，未出现用户的CPU/内存失控。该短用例不是完整压力或实机验收。
- 真实notify嵌套事件断言内层/外层/初始状态完整还原；人工User事件阻塞3.2秒时，后台在GUI恢复前已落盘UiStall，event=1000、固定QObject、depth/age和lastUi均正确。**这是人工诊断验证，不是用户Hang复现，也没有Hang修复前失败/修复后通过证据。** 另验证轮转/溢出/白名单/停止及停止后no-op。
- 现有隔离SFTP上传→局部刷新→当前选中目标删除、确认期间list到达、取消/冲突/深层刷新均通过；生产JSONL包含Upload/Delete/List/Render链。新增鼠标、monitor/process、terminal阶段从实际生产调用落盘，所有新记录版本为0.1.22。

证据：`test-results/0.1.22/ctest-final.txt`、`test-summary.json`、各`*-final.txt`、`arrow-production-windows/`、`tooltip-production-windows/`、`artificial-ui-stall.jsonl`、`sftp/`。隐藏Windows窗口均为WA_DontShowOnScreen；没有可见桌面自动化、真实服务器或用户进程转储。仍需用户再次遇到问题时提供本版logs进一步定位，真实Windows用户会话/多显示器/DPI组合及非Windows构建未验收。

## 交付

独立目录 `dist/RabbitShell-0.1.22` 和单EXE `dist/RabbitShell-0.1.22-portable.exe`。保留 .12–.21 共20目标725文件，按开工完整inventory逐项复核。最终app重新链接；文档冻结同步目录后pin approved inventory，再显式 `-Version 0.1.22` 生成payload/portable，不用脚本旧默认版本。目录验证、PE resource静态解压逐项核对及最小PATH/中文空格/仅EXE/私有设置/offscreen的version、manager、demo三项烟测，结果和app/portable/payload SHA见 `final-report.json`，改动源清单见 `source-files.json`；本文不预写包装通过。
