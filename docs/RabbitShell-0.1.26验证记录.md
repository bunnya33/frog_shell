# RabbitShell 0.1.26 验证记录

任务日期：2026-11-10。Windows x64、Qt 6.8.3、Release。

主审已通过实现、红绿测试、暗浅截图和数值日志审查，允许冻结交付。源/文档在生成 payload 前冻结；独立包的实际静态/运行结果单独见交付报告，不在本文预写通过。菜单图标、删除实际 Qt“打开文件”入口及另一路 HTML 原型均不在本轮范围。

## 已确认修复及边界

旧清屏在本地 feed ED2/CUP home，清掉提示符/未提交输入并把协议光标移到 row 0，远端行编辑器未收到坐标变化。新增两个生产菜单回归在修复前真实失败：原 row 2/1 变为 0，`terminal-red.txt` 为 6 pass/2 fail；同轮四个 1047/1049+历史进退对照原本通过，未把它们伪装为失败前证据。

清屏现用受原有 hash/anchor 门禁保护的本地 screen query/clear，保留当前完整可见软行的 cells/属性和原 row/col，只清其他主屏行及 history/selection。零 PTY 字节、不注入 VT 序列，不改 pen/modes/parser/saved cursor/pending wrap，不移动提示符；**上方留空白**。备用屏禁用以保护 TUI/隐藏主屏；软行前缀跨进历史时禁用并提示，不猜边界、截断输入或假称支持任意超长命令。硬换行多行语法无 shell integration 不能整体识别。细则见《终端清屏与切屏》。

用户截图中的 Vim、Stopped、shell returned 127、光标下方输出不被一概认定为残留；合法光标移动与全屏内容继续保留。**用户实际 Vim 编辑/退出覆盖过程尚未完整复现，独立根因未定位，不能声称彻底修复 Vim 或所有覆盖问题。** 本轮确证并修复的是清屏模型/坐标失配，另有有限切屏/绘制回归与无文本状态诊断。

## 受影响回归

`build-review.txt` 构建应用及受影响测试成功；`build-terminal-evidence.txt` 仅补链接最终截图/数值日志断言。`ctest-stage.txt` 为 **6/6 全通过**：

| 目标 | pass | fail | skip |
|---|---:|---:|---:|
| terminal | 32 | 0 | 0 |
| appearance | 31 | 0 | 0 |
| diagnostics | 5 | 0 | 0 |
| menus | 14 | 0 | 0 |
| workspace | 23 | 0 | 0 |
| editor_groups | 34 | 0 | 0 |
| 合计 | 139 | 0 | 0 |

计数含 QtTest init/cleanup；无跳过项冒充通过。未运行无关协议或压力矩阵。appearance 包含全部既有 .16 主屏重排/宽字/组合字/phantom/history/备用屏及 resize 回归，原 .16 CMake 补丁未删除或关闭。

新增 14 行终端用例覆盖：

- 清屏保留当前提示符/未提交输入、中间光标、原 row/column，其他行/历史归空、选区清理，反复清屏无旧输出复现；暗浅实际 Qt 渲染及稳定可见光标。
- 全软行包括宽字/组合码点；清屏后左右移动、插入、删除、Enter 的精确 input bytes 与合成远端响应，清屏本身零 input。1048 保存恢复、SGR/前景色、origin/scroll region 不变。
- partial UTF-8、partial CSI 和组合字符跨清屏继续解析；精确满列 pending wrap 继续输出，不截宽字或重放字符。
- 备用屏与跨历史长软行禁用及 direct-call 防护；先清屏再合成 Vim 主备屏/暂停恢复对照。
- 1047/1049 × 自动/保留 scrollbar，带多行历史和合法下方输出的三轮切屏；真实控件 resize 时保留逻辑内容、光标字形序号，继续写入原位；scrollback/失焦/恢复焦点与强制重绘图像一致。
- 真实生产数值状态日志：切屏、grid resize、成功清屏三种事件，记录随机 terminal tag、行列、光标、history/scroll/mode。fixture 明确断言无测试正文/命令串，每条小于 512 bytes；日志版本 0.1.26。沿用后台队列、轮转和人工 stall/stop 原回归，无原始流或文本采集。

`clear-initial.txt` 为实现后的定向 12/0，`alternate-resize.txt` 为 3/0；最终以六套 stage 报告为准，不重复并入总数。`legacy-clear-sequence-{dark,light}.png` 是在测试中明确喂入**旧菜单同 VT 序列**的诊断图，不冒充旧应用实机；真实生产红灯仍以 `terminal-red.txt` 为证据。`clear-input-before/after-{dark,light}.png` 和 `alternate-resize-return.png` 是实际 Qt Terminal 客户端图，分别显示保留原位输入/光标、合法下方输出。model assertions 与 repaint 对照区分数据清除和残影。

## 范围、源与旧包保护

测试使用私有 QSettings/data、offscreen 和精确 VT 流。现有 bookmark-shell 为 subprocess PIPE，不是真实 PTY；本轮未执行真实 Vim/Readline PTY、真实服务器、真实用户配置/密码/信任记录或可见桌面，也未宣称多屏/DPI实机验收。

开工 .25 完整 **187 文件**源 hash 全匹配后保存 `source-before.json`/ZIP；旧 .12–.25 **28 目标/1055 文件**完整 inventory 保存并阶段复核全部不变。完整 after 源快照、源码差异清单、截图、测试/数值日志及 stage-report 位于 `test-results/0.1.26/`。README、现行交互清单、菜单说明和第三方补丁说明同步清屏边界；package-windows 脚本已列入本规则文档与本验证记录。

冻结源/文档后生成独立 `dist/RabbitShell-0.1.26`，pin approved inventory，再显式 `-Version 0.1.26` 生成 `dist/RabbitShell-0.1.26-portable.exe`。approved/目录/generated/PE 逐项 size/SHA、version/manager/demo 三项私有 offscreen 烟测、旧包复核与准确 SHA 由包外 final-report 记录；不预写运行检查通过，也不在 pin 后修改包内文档。
