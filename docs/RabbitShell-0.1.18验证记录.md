# RabbitShell 0.1.18 实现与验证

本版仅调整服务反馈、设置恢复及主连接提示，保留远端零安装和既有终端重排补丁。

## 实现

- 服务列表常态隐藏底部计数、权限说明、刷新及部分查询 warning，并移除占位。未知项仍标“未知 / 读取失败”，原因留行 tooltip。真实连接/查询/变更失败单行省略、完整 tooltip；变更失败在后台列表刷新后仍可读，下一次成功操作收起。创建检查与步骤结果留原表单，不重放变更、不隐式 sudo。
- 设置底部新增“重置所有设置”，确认默认/ESC取消。UiPreferences 先停列宽 timer，按白名单移除 `appearance/process-table/services/network/files/downloads/window`，reload字体、倍率和Dracula，再更新已有列表基础列宽与各workspace本地视图。默认内置Cascadia Code NF Medium500、三类14px、100%；字体默认按钮仍独立。NIC回默认项，隐藏文件恢复显示，下载目录下次使用系统默认。
- 不使用 QSettings.clear；连接/分组/密码/端口规则/服务器信任记录不动。不重建文档、PTY或模型，不提交端口草稿、不重连、不刷新远端或取消传输。当前主窗口恢复正常1280×850并用既有屏幕适配限制尺寸；后续正常关闭仍会保存当前几何，并非永久禁用窗口记忆。
- 主Session实际phase驱动中心终端图标、阶段文字及3逻辑px不定进度；已有文档/终端时只用非阻断条。footer连接/等待橙色、成功绿色、失败红色。主attempt+session身份保护phase/认证/迟到读取，主连接取消不失效独立服务/传输/隧道凭据generation。隐藏/非活动/结束/取消/关闭停止动画；独立连接不驱动主提示。失败/取消可手动重连，旧主worker结束前按钮禁用，不自动重连。既有认证拒绝后提示新凭据的流程保留，明确取消会使其失效。
- 内联新建断线取消规则保持原样；保留的是已打开Editor文档与PTY历史。终端resize仅给存在且running的会话发送，历史终端不发送旧id尺寸。

## 实际验证

Release / Qt6.8.3 / Windows x64，私有临时QSettings和data-dir，QT_QPA_PLATFORM=offscreen。最终相关CTest **10/10目标通过**，含init/cleanup合计 **154 passed / 0 failed / 14 skipped**：

|目标|pass|skip|
|---|---:|---:|
|appearance|30|0|
|services|13|6|
|processes|8|0|
|workspace|21|0|
|controls|6|0|
|credentials|11|6|
|ports|9|2|
|terminal|18|0|
|editor_groups|29|0|
|core|9|0|

证据在 `test-results/0.1.18/ctest-final.txt`、`test-summary.json`及各`*-final.txt`。服务/端口未提供隔离SSH fixture，凭据的网络/原生系统后端集成项跳过；不将skip写成通过。新测试走真实设置确认框（取消零变动、确认即时与重建默认）、检查列宽timer/缓存、保护私有connections/tunnels/known_hosts文件字节及分组/secret后端调用；活动文档/终端缓冲/端口草稿和runtime/传输记录保留。连接测试主要使用未启动Session合成phase/认证取消/成功/失败/迟到信号；另用无SSH回应的本机loopback监听验证取消worker结束前重连禁用，无真实服务器或凭据。

早期失败保留：reset夹具同列宽不触发timer、屏幕尺寸预期未考虑offscreen800px屏幕；服务新测试应匹配既有完整“未知 / 读取失败”文案。初轮Workspace回归暴露内联新建断线行为被误改及历史终端resize；恢复原规则并加running guard后最终21/0。未通过修改原安全断言容忍未知任务。

实际Qt截图：`services-{quiet,error}-{dark,light}.png`、`reset-settings-{dark,light-cancelled}.png`、`connecting-{dark,light}.png`、`connecting-waiting-120-{dark,light}.png`、`connection-failed-{dark,light}.png`。offscreen可核对控件和状态/动画开关，不能代替真实桌面遮挡、硬件动画、原生模态层级、多显示器DPI或真实systemd验收。本轮未扩SSH/launcher压力矩阵或重复整体内存审计。

## 交付冻结

新目录 `dist/RabbitShell-0.1.18`，单文件 `dist/RabbitShell-0.1.18-portable.exe`；现有.12至.17十二目标415文件保留。文档先冻结并同步目录，再pin inventory，显式 `-Version 0.1.18 -Source ... -Manifest ...`；默认0.1.12流程不改，launcher核心未改。

本文冻结时不预写打包后检查通过。verify-package目录启动、portable静态与最小PATH/中文空格目录仅EXE的version/manager/demo三项离屏烟测结果，以及实际app/portable/payload SHA256与size、旧包复核、源文件清单，以 `test-results/0.1.18/final-report.json`、`source-files.json`和实际报告为准。烟测仅结束自有子进程，不代表优雅退出或真实服务器验收。
