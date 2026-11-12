# RabbitShell 0.1.37 验证记录

本版落实提示框可读性、移除连接页签重复入口、监听端口表头裁切修复，以及本地等待保留主SSH会话、同工作区重连复用终端和有界连接诊断。使用Windows x64 / Qt 6.8.3 / MSVC Release；网络验证仅访问私有loopback协议夹具，未操作真实服务器、用户配置、凭据或可见桌面。

## 实现与边界

- AppMessageBox及UploadConflictDialog正文、按钮采用14逻辑px，正文实际布局目标行高22px，覆盖中文、显式换行和无空格长路径。保留可选择的原生标签、PlainText安全、QMessageBox原grid、roles/result/default/escape/open/exec、checkbox/details和异步生命周期。提示按钮至少84×32，normal/hover/pressed/disabled均有单层描边；首次设置专属属性立即repolish，避免首次显示样式缺失。没有给全局工具栏增加边框。
- 删除serverTabs右上角新建、设置、锁定按钮及corner host；顶部连接/设置菜单、Ctrl+,和启用密码后的菜单锁定入口保留。设置菜单焦点仍保护端口草稿。编辑组、终端操作及系统标题栏未移除。
- mini/full监听表头限定上下padding为0、水平8px，字体和最小高度联合计算，保留原生排序箭头；测试按真实Horizontal header option检查文字区域，100/144%两主题中文不被垂直裁切。窄栏仍可横向滚动。
- 主Session使用非阻塞、可继续的libssh2操作；EAGAIN保留原API、参数及未完成输入缓冲，不丢弃协议状态、不重播文件作业或已发送输入。等待期间处理PTY、窗口尺寸及显式stop。本地慢等待只显示橙色提示，恢复还原原状态，重复true/false通知幂等；不因5秒阈值或footer颜色主动断开主会话。真实peer关闭、操作系统传输错误及用户主动断开仍结束连接。初次连接的可配置超时仍保留。
- 系统monitor与监听端口复用已有独立Commands连接及有界队列，不新增常驻连接或定时器；采集慢/失败保留上次指标并可见标记过期，认证拒绝停止自动重试，不连带关闭主PTY/SFTP。Commands作业仍有输出/时间上限；超限可回收该独立worker。空闲worker按提交、停止和keepalive期限唤醒。
- 同工作区、同身份重连保留终端view/frame/id、分组、名称、顺序、焦点和历史，仅为断线中断的终端重新打开shell；正常exit、用户关闭和启动失败项不自动复活。旧代输出不污染新通道且仍归还配额。仅承诺原启动目录，未实现OSC7，也不知道用户在shell内手动cd后的实时cwd；不声称恢复旧进程。新shell启动目录失败有可见提示，资源管理恢复原已提交目录，失败明确提示回退。
- 普通上传仍为原同目录.part后原子rename；没有直接目标写入或暂存目录设置。编辑保存原子替换不变。传输取消须等待在途协议步骤到安全边界，不能保证远端无响应时立即完成；用户stop可及时结束连接，不自动重放不确定操作。
- 连接诊断仅记录固定阶段、session模式、SSH错误码、耗时/阈值及主动/异常退出。只保留关键建连、异常、慢等待/恢复和退出；常规成功job、按键、resize、正常monitor及poll不逐次记录。不写原始错误文本、凭据、命令、地址、用户名或文件内容。footer毫秒值明确为系统采样响应耗时，包含远端执行及SSH往返，并非纯RTT；颜色不触发断线。

## 验证结果

最终Release构建exit0。15个受影响CTest入口全部通过，0失败，耗时59.69秒：appearance、rounded_surfaces、processes、editor_groups、workspace、controls、menus、ports、services、terminal、app_lock、credentials、file_transfers、bookmarks、diagnostics。原始Qt合计339 passed / 0 failed / 31 skipped，包含各suite初始化与清理。

skip不是通过：离屏运行未启用原生菜单合成/hover、Windows锁消息、系统凭据库和专用端口/凭据/收藏PTY夹具；部分服务、锁中传输及上传协议用例随后由下列私有fixture补足，仍保留CTest原始skip。逐项原因见包外 `test-results/0.1.37/regression/summary.json`。未再次运行本轮未改动的原生合成和OS锁通知，也未访问系统凭据库。

| 私有协议/专项 | 实际结果 | 验证内容 |
| --- | --- | --- |
| 主Session慢等待 | 5场景均exit0，每场景3/0/0 | channel-open 7s、exec 7s、read/close 9s恢复期间PTY可交互且输入一次；真实peer-close；等待中主动stop小于2s；诊断字段/隐私 |
| SSH/SFTP | 3/0/0 | 真实生产Session的PTY输入、原子编辑保存及冲突、上传/下载、文件操作、密钥认证 |
| 上传/保存专项 | 8/0/0 | 原上传规则、保留两者、保存临时文件清理、undo/覆盖屏障、事件刷新/等待关闭、上传后删除；无exec及残留.part |
| Services | 19/0/0 | 私有假systemd命令、状态、权限/部分失败、输出/时间上限，不运行真实shell |
| 锁中后台任务 | 3/0/0 | 已接受上传继续，解锁后原ServicesPanel mutationGuard可用 |
| SSH trust | 36/0/0 | Session/Tunnel信任、变更、异常记录和并发确认；结束后active transport/relay为0 |
| 三轮重连 | 3/0/0 | 队列式生产Workspace夹具，视图/历史/启动目录/资源目录/分组/焦点稳定；正常exit不复活、旧输出隔离、启动失败状态、等待通知幂等 |

before私有channel-open场景在远端仍活跃且PTY持续时4484ms报Timed out并断开；after相同延迟恢复、无失败和断开。read/close的旧证据仅为作业失败，未将其误称同一断线路径。该结果证明修正了可复现的客户端路径，**不确认用户现场两次事件的根因**。主审另用最终ModeFiles+SFTP夹具独立验证7秒exec延迟、PTY持续、peer-close与主动stop，三场景通过；正式结果和独立证据分开保存。

提示框专项及最终appearance/file_transfers回归覆盖首次显示的常态/hover/pressed/disabled边框像素、实际字体14px与行基线21–23逻辑px、长路径绘制宽度、显示后更新、详情/复选框、主题/缩放、hide/show、Enter/Escape、open+deleteLater和父对象销毁。保留已审阅的删除/未保存/冲突暗浅100/144%图，分别在 `focused-ui-2/`、`focused-conflict/`；最终监听侧栏/顶部参考为 `focused-ui-5/listening-sidebar-dark-z100.png` 和 `listening-sidebar-light-z144.png`。均为真实生产控件及离线模拟内容；未用设计稿代替。macOS/Linux、其它Windows版本、真实网络故障和实机多DPR未在本轮实测，不宣称全局零泄漏或ASan验证。

## 发布约束

开工基线为0.1.36冻结254源，旧0.1.29–0.1.36共16目标/775文件；8项批准设计引用不覆盖。源、文档、版本完成后冻结source-after JSON/ZIP，再由同一Release生成0.1.37目录及portable。approved/generated/目录/PE逐文件size和SHA-256、包内文档与源码、应用与本次Release一致性，以及隔离目录/portable启动烟测结果写包外报告。烟测使用独立data-dir，含密码锁启动与同目录第二实例唤起，不使用真实用户数据。冻结后不再修改包内文档；最终哈希与主审审计另存包外。
