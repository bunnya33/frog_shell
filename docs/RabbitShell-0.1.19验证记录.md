# RabbitShell 0.1.19 实现与验证

本版新增Windows资源管理普通文件拖出，并修正编辑页签拖出窗口时的反馈。其他平台继续使用下载菜单；不增加远端工具或agent。

## 原生文件拖出

- Windows `IDataObject` 暴露 `FileGroupDescriptorW`、带lindex的 `FileContents/IStream` 和只复制的Preferred DropEffect，不生成虚假的远端 `file://` 或本地路径URL。支持最多256个普通文件，remotePath去重；目录、链接、新建占位、来源不明和混合选择拒拖。
- Descriptor只有Windows可落地basename：无效字符替换、保留设备名加前缀、尾空格/点移除、名称截短并保留UTF-16边界、同批次重复名区分；不会暴露远端目录结构。最终落点/现存重名处理由Explorer或目标决定。
- 格式枚举、QueryGetData、descriptor、Stat及hover不下载；OLE源确认鼠标释放后，目标第一次Read才将任务投到已有SFTP队列。每文件只请求一次，QSaveFile磁盘暂存；SFTP和IStream/CopyTo均按64KiB处理，不把整个远端文件存进内存。先下载完整暂存再向目标返回内容，不声称边下载边返回。
- 下载前校验普通文件、listing的尺寸和mtime，提交前再次校验尺寸/mtime与实际字节；不符明确失败要求刷新，避免旧FD_FILESIZE造成已知截断。不提供原子远端快照，不能检测尺寸/时间均未变化的并发内容改写。
- 实现IDataObjectAsyncCapability与free-threaded marshaler，跨线程只等共享状态/condition，不跨线程访问Workspace或使用BlockingQueuedConnection。GUI同步Read使用20ms小事件循环等待，允许处理取消/关闭；同一stream的Seek/Clone重入拒绝busy，独立stream有独立游标。数据和spool存活至最后消费者及活动writer释放；取消、断开、更新身份、关闭只停自有下载，不重放。
- IDragSourceHelper生成带真实文件图标的拖影。Shell实际使用HGLOBAL及IStream私有格式，数据对象线程安全复制最多16格式/总4MiB并遵循fRelease；此上限用于拖影元数据，不是远端文件内容缓存。
- 下载任务保留暂停/取消、传输速率及完成平均下载速率。“远端下载已完成”和“系统已接收/拒绝”分开，最终落盘仍由目标负责；不保存临时目录到downloads/path，native任务不能从已结束拖出重试到失效spool，需重新拖出。
- FileView信号以QueuedConnection传值快照给Workspace，先退出Qt6.8.3 `maybeStartDrag()` 中未保护的startDrag后续访问，再进入OLE嵌套循环。原生调用前复制execute适配器，返回不访问已销毁owner/source。每批闭包弱捕获owner/batch，无累积destroyed连接。

## 编辑页签

拖影包含原文件图标与小页签。窗外IgnoreAction cursor是“小窗口＋向外箭头”；RabbitShell任意可见窗口内无效区域仍禁止，合法内部目标保持Move。继续使用DragReleaseGuard，只在明确鼠标释放、未Esc/原生取消且源仍有效时将原对象移到独立窗口。

Windows消息hook仅观察、不吞消息；feedback和release统一QCursor::pos逻辑坐标，cursor按应用倍率和源DPR绘制。核对Qt6.8.3公开源码：GiveFeedback检查QDrag::dragCursor(action) cacheKey→createCursors→SetCursor，包括IgnoreAction，因此实际修改Windows光标路径，未重写Qt OLE引擎。来源URL/哈希及只读源码在 `test-results/0.1.19/qt-reference.json`。混合DPI真实桌面未验收。

## 实际验证

Release / Qt6.8.3 / Windows x64，offscreen、私有QSettings/data-dir、隐藏进程。最终CTest **11/11目标通过**，含init/cleanup共 **171 passed / 0 failed / 16 skipped**：

|目标|pass|skip|
|---|---:|---:|
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

证据 `test-results/0.1.19/ctest-final.txt`、`test-summary.json`、各 `*-final.txt`。native常规运行两项SFTP fixture skip，在单独私有协议运行中 **4 passed / 0 failed / 0 skipped**（含init/cleanup）已完成；其他14项为未启用的服务/端口网络和原生系统凭据集成，不伪称通过。

- COM：Shell InitializeFromBitmap实际S_OK；metadata/hover/Stat不取内容，多lindex实际内容核对；20MiB+17跨线程封送流逐块写入目标磁盘并比SHA，最后引用释放spool消失；同步Read中GUI heartbeat继续、Seek/Clone重入不死锁；取消、拒绝和源销毁有错误结果。
- 私有SFTP：Python3.12及隔离Paramiko fixture，9MiB实际文件经生产Session/SFTP队列和COM stream下载并比SHA，旧尺寸明确拒绝、仅一次正常大文件open；实际慢文件取消，以及已读到数据后销毁Workspace，Read退出、worker停止、最后引用后spool目录消失。audit仅一次大文件open和两次慢文件open，零exec，不连接用户服务器/凭据。
- Qt：Ctrl/Shift多选、框选、目录拒拖、本地上传回归；页签窗外释放/应用内无效/其他窗口/Esc/原生取消/源销毁，内部move与既有草稿保留回归。queued-dispatch专项走生产连接，验证延后调用、排队后先关闭丢弃，以及执行中销毁安全返回。
- 图形材料：`native-file-drag-badge.png`、`0.1.19-tab-drag-badge.png`、`0.1.19-tab-float-cursor.png`。这些为离屏Qt真实绘制控件/图标；没有自动操作真实Explorer或可见桌面。COM消费者和私有SFTP不等于真实Explorer鼠标落点/系统冲突对话框/混合DPI实机验收。非Windows仅条件编译结构复核，未做本机平台构建。

早期真实失败保留：Shell helper要求TYMED_ISTREAM私有格式，补齐有界格式存取后成功；任务面板native条目需放批次子项避免批次更新覆盖完成文字；原运行器Python3.11缺Paramiko，改用既有Python3.12隔离测试依赖后成功。最终报告没有将这些初轮失败当通过。不重跑旧launcher压力矩阵或整体内存压力。

复现：配置Qt/OpenSSL PATH、QT_QPA_PLATFORM=offscreen、RABBITSHELL_TEST_REPORT_DIR；构建相关Release目标后运行上述11目标CTest。隔离协议使用Python3.12、PYTHONPATH指向 `.tools/test-python`，运行 `tests/run_native_drag_integration.py build/Release/rabbitshell_nativedrag_tests.exe`，不调用系统shell或真实后端。

## 交付冻结

新目录 `dist/RabbitShell-0.1.19`、单文件 `dist/RabbitShell-0.1.19-portable.exe`。保留.12至.18十四目标491文件；原libvterm补丁、字体和launcher核心未改。文档同步目录后pin inventory，显式 `-Version 0.1.19 -Source ... -Manifest ...`；脚本兼容默认仍0.1.12。

本文冻结时不预写包装检查通过。目录verify-package、portable静态及最小PATH/私有profile/中文空格目录仅EXE的version、manager、demo三项烟测，实际app/portable/payload哈希与size、旧包复核、源文件清单见 `test-results/0.1.19/final-report.json`、`source-files.json` 与实际报告。烟测只结束自有GUI子进程，不证明优雅退出或真实服务器行为。
