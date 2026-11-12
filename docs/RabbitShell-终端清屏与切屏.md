# 终端清屏与全屏恢复边界

0.1.26，2026-11-10。本文记录本轮终端修正，菜单图标和实际“打开文件”入口不在本轮改动范围。

## 已确认的问题

原右键清屏在本地把 `ESC[2J ESC[H` 当作远端输出喂入 libvterm，随后清历史。前者清除了提示符和未提交输入，并将本地协议光标移到 row 0；远端 shell/行编辑器没有收到任何重绘或坐标更新，仍以原位置编辑。生产菜单的两个修复前回归均显示原 row 2/1 被改成 0，属于模型数据被改写，不只是残影。

用户截图包含 Vim、Stopped、shell returned 127 以及光标下方旧输出。这些也可能是合法的程序/光标移动结果，不能单凭截图删除。带历史的合成 1047/1049 进退、resize、暂停/恢复序列和重绘对照目前通过，**尚未完整复现用户实际 Vim 编辑/退出后的覆盖现象，不能宣称独立 Vim 根因已定位或所有相关问题已根治**。

## 当前操作语义

主屏清屏保留光标所属的整个**可见软换行逻辑行**及原 cells/属性；清除其他主屏行、旧历史和选区。光标保持原 row/column，**上方可能留下空白，不把提示符移动到顶部**。菜单 tooltip 明示此行为。保留原坐标让后续远端左右移动、插入、删除和提交继续使用同一坐标。

不发送 Ctrl+C、Ctrl+L、Enter 或任何 PTY 字节，不伪造提示符，不停止 PTY，不重建终端，不回放文本或用空格重画输入。普通输出和切屏不执行这次清除，不对光标下方内容做自动删除。

安全限制：

- Vim 等备用屏中禁用“清屏”，保护当前 TUI 和隐藏主屏；tooltip 说明原因。菜单激活时再次检查当前模式，避免打开菜单后远端切屏造成误清。
- 若软行首部已跨入历史，禁用并解释“当前软换行延伸到历史，无法安全清屏”。历史仅存物理 cells，缺少可靠逻辑边界，不能猜前缀、截断宽字或假称支持任意超长输入。
- 没有 shell integration，无法识别包含**硬换行**的整个多行语法单元；只保留光标所属软行，不承诺保留所有多行命令/多行提示符。合法全屏/光标上移输出由远端协议控制。

## 最小本地原语

公共 libvterm 0.3.3 screen API 有 cell 读取、reset、damage 等入口，没有保留 cell 的局部清除函数。沿用既有 hash/anchor 门禁，在生成的 screen.c 追加本地 query/clear 两个函数，声明位于 `src/ui/vterm_extensions.h`，不改上游原文件、公开 vterm.h、结构或版本。

原语按 continuation 找到可见逻辑行首尾；直接把其他行置为默认色空 cells，并清其 lineinfo，最后报告 damage。保留行不搬运；cursor、DEC saved cursor、pen、modes、scroll region、origin、pending wrap、组合字符及 partial UTF-8/CSI parser 状态均不改。应用侧再清自己的 history/selection。原 .16 主屏 reflow、宽字、历史上限和 pending-wrap 补丁完整保留，备用屏仍为固定网格。

## 后续定位日志

沿用原本地有界后台日志，在切主/备屏的 feed 完成、实际 grid resize 和成功清屏后分别记录 TerminalScreenState / TerminalGrid / TerminalClear。字段只含随机终端 tag、columns/rows、cursorRow/cursorColumn、historyRows、scroll/scrollMaximum、alternate，便于关联尺寸/滚动和模式；同一 feed 内多次切换仅记录最终状态。

不写命令、终端正文、文件内容、环境、主机/用户名或凭据。无新 timer、远端探测或原始流缓存；沿用 512 条/256 KiB 队列、离锁批量磁盘写入和 3×2 MiB 轮转。人工 fixture 验证字段、无正文泄漏及 stop 后退出；不是用户 Vim 故障的真实复现。

## 验证边界

回归使用私有设置、offscreen、精确合成 VT 流和实际 Qt Terminal/libvterm cell/图像。现有 bookmark-shell fixture 是 subprocess PIPE，**不是实际 PTY**，本轮不拿它证明 Vim/Readline，也未为“真实”标签新增复杂测试基础设施。真实 Vim/Readline PTY、真实服务器、可见桌面和物理多屏本轮未验收。具体红绿报告、原 .16 重排及受影响套件结果见 0.1.26 阶段/验证记录。
