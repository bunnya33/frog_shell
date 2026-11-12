# RabbitShell 0.1.16 验证记录

Windows x64 Release，Qt6.8.3、MSVC。默认内容字重Medium500；设置提供共享等宽字体/字重和三类8–40逻辑px字号，正文焦点Ctrl缩放并持久化，服务/端口内容同步且不触发业务变更。实现/配置见[字体设置](RabbitShell-字体设置.md)。终端仅主屏启用固定libvterm0.3.3窄补丁，保留宽字、组合字符、真实空格、选区与pending wrap；源码来源/两个原文件hash、失败与修复及边界见[重排实现](RabbitShell-0.1.16终端重排修复方案.md)。原字体字节不变，MIT/OFL许可和NOTICE随包。

## 实际验证

临时QSettings/data、Qt offscreen、隐藏进程，不连接真实服务器/凭据/known_hosts或可见桌面。

| 目标 | pass | fail | skip |
| --- | ---: | ---: | ---: |
| appearance | 23 | 0 | 0 |
| terminal | 18 | 0 | 0 |
| editor_groups | 29 | 0 | 0 |
| core | 9 | 0 | 0 |
| workspace | 16 | 0 | 0 |
| ports | 9 | 0 | 2 |
| services | 11 | 0 | 6 |

CTest7/7通过，共115 pass（含init/cleanup）/0 fail/8 skip。skip为未配置网络fixture的用例，本轮不宣称SSH/systemd验收。报告在`test-results/0.1.16/ctest-targeted.txt`及七个`*-Release.txt`；未重复旧launcher27项或压力矩阵。

覆盖真实资源字体/实际400与500、SGR700墨迹差异，设置即时同步/重建持久化/无效回退/恢复默认，精确焦点与modifier/ShortcutOverride/小wheel累计/上下界，未聚焦及Find不触发Qt内置zoom，终端不发送缩放字节、原上下键正常。端口实际设置弹窗保留草稿文本/内联选区/列表选择与runtime、不提交或重启，关闭后原outside提交语义保留；服务改字体不发查询。40px行高、列最小宽与横滚不重建model。

重排验证包括中文边界前/跨/后、组合字符、真实尾空格、屏幕/历史、短行padding、超宽pop拒绝、历史上限和端点淘汰、选区序数及IME、精确满列/phantom、active/inactive primary与alt返回、两行小窗口超屏长记录；缩小/恢复后继续输入比较有序cells，捕获真实覆盖而不只去换行。初始红报告保留，最终appearance23/0/0。

实际查看两主题`appearance-*.png`正文控件、`font-settings-default-*.png`默认14/500、`font-settings-*.png`40px设置边界和`font-lists-40px-*.png`窄列表；正文图不是完整workspace截图。offscreen警告保留。DPI事件模拟不等于真实跨屏/多显示器验收，未测macOS/Linux、真实远端全屏应用或整体内存压力；仅验证单字体注册及有界算法/历史边界。

## 交付流程与限制

新目录`dist/RabbitShell-0.1.16`，新单EXE`dist/RabbitShell-0.1.16-portable.exe`；现存.12/.13/.14/.15八个目标不回写。目录显式`package-windows.ps1 -Destination ./dist/RabbitShell-0.1.16`，文档/许可冻结同步后pin inventory，再显式`package-portable-windows.ps1 -Version 0.1.16 -Source ./dist/RabbitShell-0.1.16 -Manifest <inventory>`，兼容默认仍为.12。

目录verify-package、portable static及单EXE --version/manager/demo三项烟测的最终实际结果、构建/内部app/包hash、尺寸、source-files及旧266文件复核见包外`test-results/0.1.16/final-report.json`。烟测使用最小PATH、仅此EXE的临时中文含空格目录和独立profile/cache/data；manager/demo启动后由harness结束所属子进程并核对退出码转发，不是可见桌面正常退出验收。本记录在manifest pin前冻结，最终报告不混入payload来源。

历史缩窄拆分，放大不合并其物理行；仍可能按2000行/2MiB上限淘汰。alt固定网格右侧裁切不承诺保留；任意绝对坐标主屏应用落入历史的光标及未写入尾部空白端点不承诺恢复，详见重排边界。
