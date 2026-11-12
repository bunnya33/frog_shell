# RabbitShell 0.1.7 验证记录

历史记录说明（0.1.8）：本文保留 0.1.7 当时的验证结果和命令。0.1.8 已按用户要求覆盖单终端关闭规则：忙闲均直接关闭、不确认、不探测；启动／关闭 probe、进程检测模块和专属测试已移除。因此本文的 probe 命令不再适用于当前源码，原 0.1.7 包与报告保留，当前规则见 [0.1.8 验证记录](RabbitShell-0.1.8验证记录.md)。

日期：2026-11-04。Windows x64、Qt 6.8.3 / MSVC、Release。界面测试使用 offscreen、独立配置和私有测试剪贴板；网络只用随机本机端口的 SSH/SFTP fixture。另在 WSL Ubuntu-22.04 中运行隔离 Bash/PTY 进程 fixture。没有操作可见桌面、真实服务器、用户配置、凭据或运行窗口，没有修改旧 dist，没有 commit/reset。

报告目录固定为 `D:/UserFiles/Documents/ChatGPT/RabbitShell/test-results/0.1.7`；以下日志和 PNG 名称均相对该目录。新包独立放在 `dist/RabbitShell-0.1.7`，保留 0.1.4／0.1.5／0.1.6。文件清单、字节数与最终 exe 哈希由报告目录的 package-manifest.json、package-verification.json 和 delivery-report.md 记录。

## 七项行为与实现

| 项目 | 结果与范围 |
| --- | --- |
| 局部删除刷新 | 操作 ID 关联原目标路径。成功删除只移除目标 item 并刷新直接父目录；递归部分失败刷新受影响的已加载子目录及父目录，不全量请求其他枝。相同路径复用节点，保留展开／选择／current／滚动与焦点；目录变链接等类型变化清理旧子树。请求携带 navigation generation、路径最新 ID 和 QPersistentModelIndex，已失效父节点与过期响应不能回填。 |
| 固定终端拆分 | 移除终端方向 ComboBox，底部终端组固定水平左右并排；编辑区页签原有明确向下拆分保留。没有改变终端输入、光标、Ctrl+C、Tab 或最终 resize 流程。 |
| 树内新建 | 右键捕获真实目标目录，即使根路径或原选择在别处；未加载目录先展开并加载。文件／文件夹均在树内占位输入，空名失焦取消，有名 Enter／失焦提交，Esc 取消。导航／再次新建先 finish 原草稿，原目标固定；Enter 加 FocusOut 不重复提交。非法／已知重名保留文本及错误；远端 touch 使用 EXCL，不覆盖。异步失败在仍存在的占位保留可修正输入，已导航离开的请求仍按原路径处理并报告失败，不撤销已发送创建。失效父目录／断线／销毁取消 UI 草稿。占位不参加选择、打开、删除、下载及上传落点；跨列编辑器按真实主题高度布局。 |
| 表头与列宽 | 仅表头列间增加短竖线，使用现有 divider 主题色；正文继续无网格、整卡圆角与交替底色。所有列可拖宽，full／compact 分别保存 widths-v1，校验列数与 60–4096 的合理宽度；250ms 单次合并写入，关闭前补写。未有偏好时名称列填充余宽；一旦手动调宽就保持用户值，窄区内部横向滚动。PID 刷新语义不变。 |
| 关闭检测 | 采用 NoOtherProcesses／Busy／Unknown，见下节。明确无其他关联进程或已 EOF 时直接关闭；Busy／Unknown 提供“关闭／取消”，默认取消。去重连续点击，绑定 Session、终端 ID、请求 token 和输入版本；检测期间新输入使结果作废。失联取消待确认 token 并关闭弹窗，保留已结束终端历史。 |
| 面板最大化 | 最大化仅隐藏右侧 editorPanel、保留所有对象和内部布局并扩展 terminalPanel；左栏、footer 和 OS 窗口状态不变。还原原终端高度，图标／tooltip／accessibleName 同步；最大化后隐藏终端先恢复编辑区。 |
| 网卡偏好 | QSettings 按 `network/<profile.id>/interface` 保存名称，只由 activated 用户行为写入。首次快照／重连恢复；暂缺时可用项回退但不覆盖偏好，重现再恢复。列表名称及顺序未变时不 clear/add，速率更新不干扰正在打开的列表；不同 profile 相互独立。 |

## 关闭检测的准确含义与限制

libssh2 的 channel alive 只说明通道存活，不能当作有任务，也不能用静默输出或提示符正则推断空闲。本轮不改变 `channel_shell` 启动，不运行 bootstrap，不修改远端 dotfiles，不把探测命令写入运行程序 stdin，不安装 agent，不增加周期扫描。

开 PTY 前后分别通过独立 exec 运行 POSIX `/bin/sh` 脚本。脚本需要 Linux `/proc` 和 `readlink`，不依赖 Python。沿探针祖先链（最多 24 层）找到 sshd／sshd-session 的 comm；通过 PID、starttime、父进程身份及拓扑固定该 server，避免依赖常因 dumpable 限制而不可读的 server exe。前后快照唯一新增的直接 TTY/session 子进程才可映射到新终端，基础进程必须为白名单标准路径的交互 shell 可执行文件。多个候选／身份不明直接保持 Unknown。

关闭时取新快照，核对 server 与基础 shell 的 PID/starttime、parent、TTY、session、实际 executable；同 PID exec 成其他程序视为 Busy。同 TTY／同 session／shell 后代中的存活进程都算关联进程，包括后台和停止任务；忽略僵尸，弹窗最多列五个 PID／状态及总数。没有其他进程时，仍要求 shell 自身为 S 状态、前台进程组匹配，且 wchan 属于可解释的终端读取／select／poll 等等待；其他状态 Unknown。

开终端后的最初 1.5 秒、最近 500ms 输入、worker 的 pending／queued input 或 GUI 新输入都不直接放行。只在启动建立身份与用户关闭时检测，不复用长期 idle 缓存。开终端身份探针共享约 2 秒启动预算、每次最多 750ms；关闭探针最多 1500ms；累计 stdout/stderr 上限 512KiB，最多 4096 个进程。要求完整 END 计数、严格协议解析、远端 channel closed 与退出码 0；stderr、超时、截断、权限／隐藏进程限制或不支持平台均 Unknown。探针通道以非阻塞 libssh2 调用清理，EAGAIN 延后由既有 worker 循环回收；探针超时不经普通 execute 的 TIMEOUT 路径主动断开 SSH，真实 socket 故障仍由正常 PTY 读写／保活发现。

检测期间同一个 worker 暂停常规 PTY 泵，可能产生上述有界的局部响应延迟；这不是实时或原子的进程快照。进程可能在检查后变化。基础 shell 自身不算“其他进程”，因此不能识别所有 shell 内建行为，尤其 `read -e` 等可能与提示符读取呈相同等待状态；启动脚本中的此类行为也无法严格证明已到提示符。不能把 NoOtherProcesses 称为严格 prompt-idle 或“所有命令都已结束”。完全脱离原 TTY/session 且已失去后代关系的 nohup/daemon 不保证检出，也不一定受关闭 PTY 影响。未知内核等待点、非支持 shell 路径、非 Linux/OpenSSH 或受限 exec 都保守确认。

## 最终构建与回归

`cmake --build build --config Release --parallel 8` 成功，最终成功日志 `build-release-final.txt`；相关 CTest **7/7 通过**，日志 `ctest-final.txt`。

| 套件 | passed | failed / skipped | 归档报告 |
| --- | ---: | --- | --- |
| core | 9 | 0 / 0 | core.txt |
| editor_groups | 29 | 0 / 0 | editor_groups.txt |
| terminal | 17 | 0 / 0 | terminal.txt |
| processes | 8 | 0 / 0 | processes.txt |
| controls | 6 | 0 / 0 | controls.txt |
| workspace | 11 | 0 / 0 | workspace.txt |
| terminal_process | 5 | 0 / 0 | terminal_process.txt |
| loopbackTerminalLayoutDoesNotRestart | 3 | 0 / 0 | pty-layout.txt |
| loopbackTerminalProbeFailurePreservesChannels / timeout | 3 | 0 / 0 | pty-probe-timeout.txt |
| loopbackTerminalProbeFailurePreservesChannels / truncated | 3 | 0 / 0 | pty-probe-truncated.txt |

计数包括各套 init/cleanup。没有重跑无关认证全套、内存压力审计或执行生产配置。

新 Workspace 回归在真实控件与未启动的 Session 队列上验证操作：局部删除、递归部分失败、类型变化、迟到列表、真实右键目录菜单、非空名实际点击面包屑／双击目录／再次新建、重名／非法／远端失败修正及一次提交、最大化／高度恢复与对象身份、两 profile 名称顺序变化／暂缺／重现。名单不变同时检查 modelReset、rowsRemoved、rowsInserted 与持久索引身份。关闭回归检查去重、新输入作废、默认取消、Busy/Unknown 保留、NoOtherProcesses 自动关、EOF 和断线弹窗清理。进程新回归实际拖动 header，验证快照不重置宽度、两视图独立保存和重新创建恢复。

`tests/probe_terminal_processes.py` 从 C++ 源提取 **同一生产脚本**，在 WSL 中直接执行，留下 `terminal-process-probe.sh`、`proc-*.txt`、`linux-pty-probe.json`。10 个场景包括无支持祖先、启动前／等待输入、后台 sleep、停止任务、任务结束、前台 sleep、运行中的内建循环、多个 TTY 候选及 exec 替换 shell。完整快照由生产 C++ parser/classifier 验证；另用紧凑构造样本检查 PID/starttime／父关系失配、不可读 executable、等待点未知、截断和尺寸上限。实际观测每次有效脚本采样约 13–15ms，不是远端延迟承诺。

该 Linux fixture 的 Bash、PTY、/proc 与后台／停止进程是真实的；父进程仅在自己的 comm 中模拟 sshd-session 祖先拓扑，**不是实际 OpenSSH 服务器验证**，没有安装 sshd 或服务。临时 shell 用 `--noprofile --norc`，不读取用户启动文件；退出只清理本 fixture 创建的 session。生产不依赖这个 Python harness。

Windows SSH fixture 只 echo，不执行 shell。布局用例覆盖迁移、浮窗、最大化／还原及未知取消后的原 PTY 连续性；另外两次独立 fixture 分别让探针响应延迟 2.5 秒或返回不完整协议，验证超时／截断后旧 PTY 与新建第二个 PTY 均继续 echo，取消关闭不发终止。它不证明真实 shell 补全、SIGINT 响应或 OpenSSH 拓扑。

## 两主题生产控件截图

| 内容 | Dracula | GitHub Light |
| --- | --- | --- |
| 树内完整名称输入 | workspace-dark-inline.png | workspace-light-inline.png |
| 仅右侧最大化 | workspace-dark-maximized.png | workspace-light-maximized.png |
| 恢复原面板高度 | workspace-dark-restored.png | workspace-light-restored.png |
| 完整进程表 header 竖分隔 | process-dracula-full.png | process-github-light-full.png |
| 窄概览表 header 竖分隔 | process-dracula-mini.png | process-github-light-mini.png |

使用生产 QWidget::grab，未重绘替代界面。已亲自查看最新两主题 inline，文字没有竖向裁切，输入横向铺满剩余行宽；主代理已审查 inline、header、两主题面板。原终端光标、分组与 ComboBox 图片由相关既有用例继续归档。offscreen 不包含原生 OS 外框合成，不替代混合 DPI、真实跨屏与 macOS/Linux 客户端验收。

## 复现命令

在仓库根目录执行，报告环境变量使用绝对路径，避免 CTest 的 build cwd 歧义：

```powershell
$env:PATH="$PWD/.tools/Qt/6.8.3/msvc2022_64/bin;$PWD/.tools/Qt/Tools/OpenSSLv3/Win_x64/bin;$env:PATH"
$env:QT_QPA_PLATFORM='offscreen'
$env:RABBITSHELL_TEST_REPORT_DIR='D:/UserFiles/Documents/ChatGPT/RabbitShell/test-results/0.1.7'
cmake --build build --config Release --parallel 8
wsl -d Ubuntu-22.04 -- python3 /mnt/d/UserFiles/Documents/ChatGPT/RabbitShell/tests/probe_terminal_processes.py /mnt/d/UserFiles/Documents/ChatGPT/RabbitShell/test-results/0.1.7
ctest --test-dir build -C Release -R '^(core|editor_groups|terminal|processes|controls|workspace|terminal_process)$' --output-on-failure
$env:PYTHONPATH='.tools/test-python'
& 'C:/Users/JOKER/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' tests/run_integration.py build/Release/rabbitshell_credential_tests.exe loopbackTerminalLayoutDoesNotRestart
foreach ($probeMode in @('timeout','truncated')) {
    $env:RABBITSHELL_FIXTURE_PROBE_MODE=$probeMode
    $env:RABBITSHELL_TEST_REPORT_DIR="D:/UserFiles/Documents/ChatGPT/RabbitShell/test-results/0.1.7/probe-$probeMode"
    & 'C:/Users/JOKER/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' tests/run_integration.py build/Release/rabbitshell_credential_tests.exe loopbackTerminalProbeFailurePreservesChannels
}
Remove-Item Env:RABBITSHELL_FIXTURE_PROBE_MODE
./scripts/package-windows.ps1 -Destination ./dist/RabbitShell-0.1.7
```

打包脚本同时带通用 Qt 指南、本记录及既有项目文档。这里的检测时限和进程数限制是明确实现边界，不是全应用性能或零泄漏承诺；本轮沿用既有低内存生命周期策略，没有全局轮询、动画或额外常驻服务。
