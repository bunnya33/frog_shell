# RabbitShell 0.1.20 实现与验证

任务日期：2026-11-09。修复本地上传目标、实际冲突交互与深层刷新；不增加远端工具或 agent。

## 实现

- FileView 的纯目标解析同时用于拖入预览、drop、右键菜单。目录整行（包括大小列、缩进/展开区域）指向该目录；普通文件行指向父目录；空白指向已成功载入的浏览根。只接收确实存在的本地文件/目录 URL，强制 Copy；链接/草稿/未知目标拒绝，不空路径回退根。整行边框与「上传到 /完整路径」预览不改选择，离开/投放/隐藏/目标移除或数据变化清理。
- 右键菜单打开时捕获目标，文件选择器期间的选择/导航变化不改投；回程核对 Workspace、Session、凭据代次及在线状态。队列 Job 保存绝对远端路径。
- 已确认旧右键 lambda 未传目标、普通文件行/空白依赖空路径回退、刷新只重新列根且保留旧子树，以及上传前无条件询问重名。Qt6.8.3 itemAt/indexAt 通常支持任意列；实际事件用例确认大小列正常命中，不将它虚构为根因。未对用户真实服务器复现旧版所有目录行症状；本版各类投放通过实际队列和隔离落盘验证。
- 上传队列默认 ask，无同名不弹框。worker 对真实 SFTP lstat 冲突发 Session/job id/逐次 nonce 请求，GUI 非阻塞显示三个直接按钮：「取消上传」「替换文件」「保留两者」，无下拉/跳过按钮。可勾选应用到本批次后续冲突；保留两者自动加序号、不覆盖原文件；类型不一致或链接禁用替换。默认/Esc 取消当前根任务及其递归传输；其他排队根任务保留。核心 skip 和旧下载策略保留。
- 每次响应核对当前 dialog、Session、job 和 nonce，worker 接受后才能更新 queued Job 的批次策略。worker 仅保存当前一个批次策略，无无限缓存。等待用 condition，可被 cancel/stop/关闭唤醒，不计入 activeMs；完成仍显示平均速率。
- 只有明确 SFTP NO_SUCH_FILE 是不存在；权限/连接/缺少类型属性均失败。已存在的普通目录可合并，文件/目录/链接不匹配不主动删除。文件以 64KiB 写独占临时文件，保留权限/可取得的 uid/gid。未经替换授权先用不覆盖 rename；提交期间新同名重新询问/改名，最多8次提交尝试，失败/取消清临时文件。明确替换使用已有 posix-rename 能力，失败不回退删除旧目标。
- 手动刷新只推进一次目录代次，从当前根结果逐层查询仍存在的展开可见目录，复用原节点，不重建树。收起/隐藏已加载枝保留子树但标未加载，下次展开重新读取；上传完成或部分失败仅刷新受影响的已加载父目录。旧导航、删除父项、同路径类型变化的迟到结果拒绝；展开、选中、滚动、焦点和未提交草稿保留。readdir 每项先清 attrs、检查类型 flags，大小沿用此次 readdir；缺少大小显示未知，tooltip 同时含精确 bytes 与人类可读单位。

## 实际验证

Release / Windows x64 / Qt6.8.3。所有 Qt 运行使用 offscreen、私有配置、隐藏进程；网络仅本机私有 SFTP fixture。

CTest 12/12 目标通过，含 init/cleanup 共 **179 passed / 0 failed / 18 skipped**。最后仅取消状态文字增加后，再复测 file_transfers/workspace 2/2；未重复整套压力矩阵。

|目标|pass|skip|
|---|---:|---:|
|file_transfers|8|2|
|native_drag|10|2|
|appearance|30|0|
|services|13|6|
|processes|8|0|
|workspace|23|0|
|controls|6|0|
|credentials|11|6|
|ports|9|2|
|terminal|18|0|
|editor_groups|34|0|
|core|9|0|

证据：`test-results/0.1.20/ctest-final.txt`、`ctest-cancel-final.txt`、`test-summary.json` 和各 `*-final.txt`。

- 真实 Qt drag enter/move/drop → 队列 Job 验证 A/B、大小列/缩进整行、嵌套文件父目录、空白根；保留选择，链接/草稿/非本地/不存在 URL/非 Copy 拒绝，离开/隐藏/目标删除清预览。菜单专项3/0/0：真实 QFileDialog 打开时改变当前目录和选择，上传仍命中原文件父目录。没有生产测试 hook。
- 纯 UI 及实际 SFTP 同时验证根文件和展开深层文件外部改写后大小更新，保留节点身份、展开和选中；合成用例另外验证滚动/焦点/草稿、收起后重新展开、父删除/类型变化/导航旧 reply 拒绝。
- 上传独立 SFTP **4/0/0**（两项业务用例及 init/cleanup）：实际 A/B/嵌套文件行/空白根落盘；无冲突零弹窗；替换、保留两者、内部 skip、取消、批次及递归；真实 GUI 替换+批次复选框、默认/Esc 取消和任务「已取消」；旧 nonce/旧 dialog 不污染新等待或批次；等待时关闭 Workspace，worker 退出、无临时残留。fixture 在提交 rename 时制造新同名，原文件保留/改名或取消；stat 权限错误、原子 rename 失败明确失败且清理临时文件，零远端 exec。
- 0.1.19 原生拖出独立 SFTP 回归 **4/0/0**，含实际下载 SHA、变更尺寸拒绝、慢文件取消和活 writer 关闭清理。常规 CTest 中两项 upload 和两项 native fixture skip 已在这两次独立运行覆盖；其余14项是未启用的服务/端口/凭据集成，未伪称通过。独立报告与 CTest 总数不重复相加。
- 最新暗浅真实 Qt 绘制：`upload-preview-dark/light.png`、`upload-conflict-dark/light.png`。窗口是三个直接按钮及批次复选框，不是下拉；真实 Explorer/可见桌面/混合 DPI 未验收。

初轮失败保留：菜单测试 timer 同步触发 action 后无法在同一 timeout 重入，改独立 singleShot，并等待文件模型后实际填写 filename/Open，加入有界超时；不为测试改生产架构。一次 fixture 的预设空白坐标实际落在前序新增目录，SFTP audit 定位后隐藏无关测试行并先断言确为空白，最终通过。源码初编一处 QModelIndex 条件类型歧义已显式转换修正。未降低业务断言。

## 交付和边界

目录 `dist/RabbitShell-0.1.20`，portable `dist/RabbitShell-0.1.20-portable.exe`；保留 .12–.19 十六目标568文件。文档同步目录后 pin approved inventory，再显式 `-Version 0.1.20 -Source ... -Manifest ...`；兼容默认仍0.1.12，不误用旧默认。launcher 核心、字体/许可、libvterm 重排补丁未改。

本文不预写包装校验已通过。实际目录 verify-package、portable 静态、最小 PATH/私有 profile/中文空格目录仅EXE的 --version、manager、demo 三项烟测，以及内部 app/portable/payload SHA/size、旧包核验和源文件清单见 `test-results/0.1.20/final-report.json`、`source-files.json` 与实际报告。包装后不再变更 payload 来源文档。

无真实服务器/系统凭据/用户信任文件/可见桌面操作。隔离 SFTP 不代表实机服务器矩阵；不证明不遵守 SFTP 不覆盖 rename 语义的服务器安全，也不保证用户确认替换后路径不再被并发修改。目录合并不提供整体事务，单文件失败保留逐项结果。下载旧策略及 .19 原生拖出边界保留；非 Windows 未做本机平台构建。烟测仅启动后结束自有 GUI 子进程，不等于优雅退出或真实服务器验收。
