# RabbitShell 0.1.36 验证记录

本版实现编辑页签共享底边、进程本地筛选、监听端口概览及独立页签、顶部应用菜单、共用标准提示框和 Windows 菜单透明圆角。沿用 0.1.35 应用锁及收藏终端组行为。未访问真实服务器、凭据、配置或系统剪贴板；数据、协议服务与截图均使用隔离夹具。

## 实现范围

- `PanelTabs` 统一拥有 header 底线，声明高度包含边线。原生 tab/corner 只占内容带，pane 预留边线，`UiZoomStyle` 对这类 panel 取消原生 tab-base overlap。移除页签和 corner 重复底边，不用位置偏移补缝。空组、滚动按钮和分组仍由真实 Qt 控件处理。
- 进程筛选为名称/PID/用户的本地纯文本、忽略大小写匹配；明确范围为已采集的前200个CPU进程。刷新继续复用稳定PID对象并保留条件、选择、排序、列宽和滚动；无匹配及清空恢复有明确状态。
- 监听端口优先 `ss`、降级 `netstat`，不安装、不提权、不扫描 `/proc/PID/fd`、不写远端文件。专用 Commands Session 隔离超时/输出异常，每工作区最多一条连接及一个在途查询；复用监控tick，前台至少5秒、后台至少15秒，单次2秒/512KiB/4096端点上限。明确认证拒绝停止自动重试；晚到回调校验 worker、凭据代次和主连接尝试。TCP仅LISTEN，UDP仅未连接的绑定端点。PID/进程为可见归属之一，完整页与tooltip说明可能共享端点。失败保留旧数据并显示过期；未就绪和断线不伪装成有效空列表。
- 六个顶部菜单复用现有工作区命令；菜单打开前焦点、当前工作区与锁代次均校验。无可用文件粘贴时隐藏粘贴。终端复制直接调用复制功能，未模拟Ctrl+C。主题切换共用完整工作区刷新。动画只改变弹层opacity，110ms，并遵循Qt菜单淡入设置。
- `AppMessageBox` 保留 QMessageBox 原生 grid、标签、按钮、roles/result 和 open/exec/finished。只重排布局项；Qt重建layout时重新捕捉。安全默认取消/否，显式默认不改，仅确定时Enter可完成。标准按钮中文化，业务自定义文字保持。40px图标、20px正文边距、14px图文间距、单线footer、至少80×28按钮；长正文滚动、无空格长路径按实际文本布局换行。已批准的 UploadConflictDialog 未改。
- Windows popup 创建前设 frameless/per-pixel alpha，绘制清透明；不能只凭DWM设置成功认定圆角有效。alpha不可用时保留轮廓回退。

## 已完成验证

Windows x64 / Qt 6.8.3 / MSVC Release 构建通过，日志 `test-results/0.1.36/build-final.txt`。构建原有命名遮蔽警告仍存在，未作为本版无关清理范围。

15个受影响CTest入口运行49.70秒：14通过、0失败，`sshtrust` 因未提供 `RABBITSHELL_TUNNEL_FIXTURE` 跳过。原始Qt报告合计330 passed / 0 failed / 32 skipped（包含各suite初始化及清理）。覆盖 appearance、rounded_surfaces、processes、editor_groups、workspace、controls、menus、ports、services、terminal、app_lock、credentials、file_transfers、bookmarks、sshtrust。

随后以私有loopback夹具补充相关协议/生命周期验证：

| 运行 | 实际Qt结果 | 边界 |
| --- | --- | --- |
| SSH trust | 36 / 0 / 0 | Session及Tunnel信任记录、变更、非法记录、并发确认；审计exec/PTY/shell/direct及最终活动transport/relay均0 |
| Services | 19 / 0 / 0 | 命令、UI取消、创建/删除、部分失败、输出上限/超时；假systemd，不运行真实shell |
| SFTP | 5 / 0 / 0 | 私有上传/事件刷新/等待关闭/上传后删除；无远端exec及残留临时上传 |
| 锁中原服务及传输 | 3 / 0 / 0 | 已接受上传继续，解锁后原ServicesPanel mutationGuard仍可用 |
| Windows锁事件 | 3 / 0 / 0 | 隐藏测试窗口模拟WTS及suspend消息路由；没有锁用户电脑 |

信任runner第一次在测试开始前读取新建ready文件遇到Windows句柄竞争，保存 `integration-trust/attempt-1-fixture-ready.txt`；未改源码或断言，原样重跑36项通过。各补充结果独立列出，不把CTest原始skip改写为通过。其余未启用的凭据/端口/PTY专用集成用例保留skip，逐项原因见包外 `verification-summary.json`，不声称所有网络集成全跑。

页签使用两主题 × 100/120/144%应用缩放 × 空/单/多/溢出/分组 × DPR 1/1.25/1.5/2，共120画面状态，真实控件像素底线、相邻重复边、正文起点、tab/corner高度及点击命中均通过。DPR为强制缩放仿真，不是四台显示器实测。已有小屏、布局恢复、编辑文档保留、终端光标/清屏/Alt-screen及应用锁回归也通过对应suite。

共用提示专项覆盖显示后更新正文/信息、复选框/详情、主题/缩放、隐藏重显、长文滚动、显式默认和标准按钮中文文案、通过基类更新正文、Enter实际finished结果、Escape、open+deleteLater、父对象销毁及原生标签身份。长路径的实际行宽未越过label宽度；这是生命周期与绘制回归，未宣称做过ASan或全局零泄漏证明。

Windows合成probe只显示独立测试窗口并截其内部区域。先验证固定底色 `#90C8E0`，再比较暗浅主题淡入中及结束后四角，单通道差不超过8；4图16角通过，当前alphaReady=true、无需region fallback。原生QWindow路径首次点击、hover换菜单及Escape通过；其余测试离屏/隐藏运行。macOS/Linux及其它Windows版本、真实多屏未实机验证。

## 截图与发布验收

`test-results/0.1.36/final-visual/` 为真实生产控件离线数据：workspace、manager、system-info、listening-ports暗浅共8张1440×900客户区图，含新顶部菜单、不含OS标题栏；另含删除/未保存确认100/144%暗浅图。`final-menu/` 为应用菜单，`native-popup-2/` 为受控底色桌面合成证据；页签局部/全幅及几何JSON位于 `focused-final/` 与 `dpr-*`。

本轮开工基线为0.1.35冻结243源文件，旧0.1.29–0.1.35共14目标/673文件。开工和发布前清单分别保存；批准的8项设计参考不覆盖。源、文档、版本完成后冻结source-after JSON/ZIP，再从同一Release生成0.1.36目录及portable，逐项比较approved/generated/目录/PE的大小及SHA-256、包内文档与源码、应用与本次Release。隔离烟测包括version、管理页、demo、密码锁启动及同目录第二实例唤起；不会使用真实用户数据。最终冻结哈希、包哈希及烟测结果写包外报告，避免冻结后再改包内文档。
