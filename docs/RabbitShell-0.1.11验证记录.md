# RabbitShell 0.1.11 验证记录

本轮接入真实本地 TCP SSH 端口转发。Windows x64、Qt 6.8.3、MSVC Release；验证使用 offscreen、临时配置、内存凭据后端及隔离 loopback SSH fixture。没有访问用户服务器、读取真实凭据、修改用户 VSCode／远端环境或操控可见桌面。没有实现远程转发、SOCKS5、自动重连或网页分享。

## 实现范围

- 底部“终端／端口”页签与三列表；普通规则使用 model/delegate 绘制，只为正在编辑的一行创建原生输入控件。空态或列表末尾只有一个添加入口。
- 严格整数 1–65535；远端输入默认同步本地值，手动改本地后不再覆盖。Enter／整行失焦保存为关闭；同一行 Tab 不提交，Esc 取消，切页暂存。运行中草稿需明确应用并重启。
- 原生开关由真实异步状态驱动，处理中禁用，失败回关并显示原因。菜单支持编辑、复制本地地址、高级地址／备注／自启动、删除；删除运行规则确认一次。非默认地址显示在原端口单元格次行。
- 隧道独立于交互 Session／SFTP 与终端数量。最后规则重启等待旧服务 idle 并销毁后新建，取消排队规则不误停其他规则。主动断开／关闭／退出会停止转发，工作区等待实际回收。
- 规则以服务器 profile.id 隔离原子保存；不保存秘密。使用现有凭据服务、修订号与身份保护，同值密码保存通知不递增 generation。未知主机仅合并当前待确认请求，不跨连接缓存批准；每个弹窗 token 防止旧对象销毁误取消新提示。
- 两页共用原有最大化／隐藏及高度恢复；最大化保持相同四角形状和图标颜色，仅选中背景／边框变化。主题切换不重建终端、编辑器或端口配置。

“已建立”仅表示本地监听和专用 SSH 连接就绪，不宣称目标业务健康。目标不可达或 sshd 禁止转发可能在实际客户端打开 direct-tcpip 时才发现；逐流错误保留对应规则的监听状态。本地占用不静默改号。

## Qt 布局与信任提示定向证据

`test-results/0.1.11/ports-layout-trust.txt`：5 passed、0 failed、0 skipped。覆盖两个同指纹待决请求合并、批准后清除临时 known_hosts 必须重新确认、旧同指纹弹窗延迟销毁不影响新弹窗，以及两主题截图布局断言。

主审发现初版 PortsPanel 在自身 resizeEvent 读取了布局前的旧 viewport 宽度，导致工作区列宽未填满，320px 第三列消失。现在在 table viewport 的真实 Resize 中更新三列并重新定位草稿；窄列将保存／取消纵向排列，按换行后的高度计算草稿行高。测试逐列断言左右边界在 viewport 内、三列宽之和等于 viewport，另断言错误标签及动作完整位于草稿内、互不重叠、错误换行高度足够。主审已目视核验修复后的暗色 320px 草稿与浅色工作区。

生产 QWidget::grab 证据（同目录）：

- `ports-dark-workspace.png`、`ports-light-workspace.png`
- `ports-dark-maximized.png`、`ports-light-maximized.png`
- `ports-dark-320-draft.png`、`ports-light-320-draft.png`

这些视觉样本中的运行／失败状态由测试设置，不作为网络成功证据；真实链路由独立 loopback 测试验证。

## 初次失败与处理

`ctest-ui-initial.txt` 保留首轮结果：editor_groups、workspace 通过；ports 为 8 passed、3 failed。两个 loopback 项在未提供 fixture 时错误进入：测试读取 QJsonObject 时使用非 const 下标插入了空 port 键。已改为 value()，并按有效 fixture port 判断 QSKIP。普通 CTest 的这两项应明确跳过，真实 runner 必须零跳过。

信任队列首次测试未显示第二个提示。现在下一次显示排到事件循环，避免与前一个 QMessageBox 的 finished／隐藏处理交错；QMessageBox::Yes 结果与每次唯一 token 共同处理批准和销毁。保留原安全要求，无长期批准缓存，诊断 qInfo 已移除。定向重测已通过。

`ports-integration-initial.txt` 保留首轮 UI 集成审计失败：两个 Qt 行为项通过，但 fixture 的 SFTP 计数为 0。原因是默认纯隧道夹具直接拒绝 session channel，普通会话在申请 SFTP subsystem 前已失败；这不能作为真实 SFTP 拒绝证据。UI runner 使用显式拒绝模式验证这一分支，核心默认模式仍禁止任何 exec／PTY／shell／SFTP 请求；不通过删审计断言掩盖未执行的路径。

## 真实 UI 集成

`ports-integration.txt`／`ports-ui-tests.txt`：11 passed、0 failed、0 skipped。`ports-ui-audit.json` 记录 6 次握手／认证、8 次 direct-tcpip、SFTP 恰好 1 次，exec／PTY／shell 均为 0，最终 active_transports／active_relays 均为 0。UI runner 显式传 `--ui-sftp-rejection`；该模式仅接受 session channel 后拒绝 SFTP subsystem，默认核心模式没有放宽。

- 仅一条运行规则在实际 Qt 行内修改本地端口、点击应用并重启：等待旧 facade 销毁，使用新监听转发 40,011 bytes（含 NUL），旧端口可重新绑定；停止待重启项后不再启动。再次启动后带活动转发关闭工作区，shutdownComplete 后 facade 已清空且监听端口可重新绑定。
- 普通 Session 发出 authenticated，随后实际 SFTP 请求失败并 disconnected；失败文本含 SFTP。并行转发正常，保存同一密码的 repository changed 确实到达，credentialGeneration 不变。
- 关闭最后一个终端不影响数据；停止最后规则并等待 facade 回收后，在 SFTP 已失败且 0 个终端的工作区重新手动启动，字节往返仍一致。
- 取消仅 UI 排队的另一规则不将健康服务标记 retiring；连接身份更改阻止新的规则，但旧规则继续转发。仅独立隧道在线时主动断开会确认、结束并释放端口。
- 同套件另验证删除运行草稿、删除保存失败时的真实 stopped 状态、高级弹窗状态重验、跨规则草稿保留、页签／最大化／隐藏／主题切换不丢对象或草稿，以及前述信任与布局。

补充关闭检查的首次运行中，Qt 仍为 11/0/0，但 fixture 审计 active_transports=0、active_relays=1，因此 runner 正确判失败；证据单独保留为 `ports-shutdown-audit-failure.txt/json`。旧 fixture 先写 relay 记录，再执行 target.close／channel.close，最后才递减计数；关闭抛异常会跳过递减，记录也不能证明关闭已完成。原失败样本没有线程异常堆栈，不能仅靠单次重跑通过认定具体根因。以下窄修复及连续复测补齐这一证据。

### relay 清理异常修复与连续复测

主审授权接管 `tests/tunnel_fixture.py` 的 relay 清理分支。现在分别尝试关闭 target 与 channel，一方抛异常仍尝试关闭另一方；在关闭尝试全部结束后才写 relay 记录，将传输错误与 `cleanup_errors` 分开，后者保留资源名、异常类型／内容及完整 traceback。最外层 finally 在 worker 即将退出时递减 active_relays，关闭若仍阻塞则不会提前归零。没有修改生产 core／UI、重新编译 EXE 或放宽审计。

`tests/run_ports_integration.py` 保持 exec／PTY／shell=0、SFTP 恰好 1 次、active_transports／active_relays=0 的断言；若 80 次清理等待后仍有活动资源，额外通过 fixture 的 `stacks` 命令保存 `ports-fixture-stacks.json`，包含线程名、标识和当时调用栈，再照常判失败。stderr 仍独立保存。

同一真实 UI 完整套件连续运行 4 轮，每轮启动全新临时 fixture，结果集中在 `test-results/0.1.11/relay-cleanup/summary.json`：

| 轮次 | QTest passed/failed/skipped | SSH/direct/SFTP | 最终 transport/relay | 关闭异常 |
| --- | --- | --- | --- | --- |
| run-1 | 11/0/0 | 6/8/1 | 0/0 | 无 |
| run-2 | 11/0/0 | 6/8/1 | 0/0 | 无 |
| run-3 | 11/0/0 | 6/8/1 | 0/0 | channel.close 的 EOFError |
| run-4 | 11/0/0 | 6/8/1 | 0/0 | channel.close 的 EOFError |

每轮 exec／PTY／shell 均为 0，8 条 relay 都在关闭尝试完成后留下记录且 channel.closed=true，stderr 均为空。第 3、4 轮实际 traceback 为 Paramiko `Channel.close → Transport._send_user_message → Packetizer.send_message → write_all → EOFError`，证明真实退出时此异常路径会发生，且新 finally 在这一路径也回收计数；原先那次失败缺少堆栈，仍不把它与某个具体异常一一认定。四轮均未超时，不需要残留线程栈抓取。

各轮报告、审计及完整异常栈位于 `relay-cleanup/run-1` 至 `run-4` 下的 `ports-ui-tests.txt`、`ports-ui-audit.json`、`ports-integration.txt`、`ports-fixture-stderr.txt`。额外一次最窄故障注入同时使 target.close 抛 OSError、channel.close 抛 EOFError，确认两次关闭都被尝试、两份 traceback 保留，线程实际退出后 active_relays=0；证据为 `relay-cleanup/injected-close-errors.json`。这项注入不是 SSH 协议成功证据。

既有 10 target CTest、core 20/0/0、凭据／PTY 8/0/0 与部署烟测没有重复执行；它们保持以下原始结果及范围。core 20/0/0 使用清理修订前的夹具，生产 core 与其协议断言均未改变。连续 UI 复测验证的是修订后的夹具和同一生产 EXE／测试 EXE。

## 最终完整回归

全部 10 个 CTest target 运行一次修正后的最终完整回归，`ctest-all-final.txt`：9 passed、1 skipped、0 failed，14.54 秒；ssh_sftp 因无 fixture 整项跳过。各 QTest 数量如下（均含 init／cleanup）：

| target | passed | failed | skipped |
| --- | ---: | ---: | ---: |
| core | 9 | 0 | 0 |
| editor_groups | 29 | 0 | 0 |
| terminal | 17 | 0 | 0 |
| processes | 8 | 0 | 0 |
| controls | 6 | 0 | 0 |
| workspace | 16 | 0 | 0 |
| ssh_sftp | 2 | 0 | 1 |
| credentials | 9 | 0 | 6 |
| tunnels | 5 | 0 | 15 |
| ports | 9 | 0 | 2 |

网络跳过不算协议通过。credentials 的 6 个跳过中有 5 个需 loopback，另 1 个系统凭据库用例需显式 `RABBITSHELL_TEST_NATIVE_CREDENTIALS=1`；本轮没有启用，不读写实际系统凭据库。其余凭据行为使用隔离内存后端。`credentials-pty-integration.txt` 另记录 8 passed、0 failed、0 skipped，覆盖登录／重连／独立传输、PTY 布局与浮窗收回、直接关闭保留另一 PTY、未保存保护、私钥纠正、保存失败不重复及握手失败保留凭据。

初次全量普通 CTest 结果另保留 `ctest-no-fixture-initial.txt`：仅修 port 读取后仍有空 targets 被配置辅助函数插入。用户授权修复同一无 fixture 初始化问题，测试的只读 port／targets 均改 value()，不修改生产 core。最终普通 tunnels 是 5 passed／15 skipped；与下面真实 fixture 20/0/0 明确分开。

## 核心协议与资源

core 代理最终证据确切文件名为 `test-results/0.1.11/tunnel-core-tests.txt` 与 `test-results/0.1.11/tunnel-core-audit.json`，20 passed、0 failed、0 skipped。生产 core 最后改动后完成该轮测试；之后只修测试无 fixture 读取，生产源码未变。核心实际文件为 `src/core/tunnel.cpp`、`src/core/tunnel.h`、`src/core/sshtrust.h`（共享信任辅助为 header，无 sshtrust.cpp）及 `src/core/session.cpp` 的信任辅助接入；测试为 `tests/test_tunnel.cpp`、`tests/tunnel_fixture.py`、`tests/run_tunnel_integration.py`。

纯隧道审计：57 次 SSH 连接、110 次 direct-tcpip、2 次目标拒绝，exec／sftp／pty／shell 均为 0，最终 active_transports／active_relays 均为 0，relay_peak 为 65,536 bytes、所记录 SSH 接收窗口均为 65,536。覆盖随机双向多规则、并发限额、客户端／目标半关闭、尾部数据、停止期间取消、退休边界立即启动／重试、指纹变化、密码拒绝、私钥认证、慢收背压和销毁。已有 Session SSH/SFTP 的独立 fixture 回归另为 3/0/0。

同轮 Release 资源诊断：

- 8 个暂停读取客户端：Private Bytes 6,549,504 → 6,549,504 bytes。
- 反复生命周期：句柄 205 → 205，线程 9 → 9；Private Bytes 3,719,168 → 3,878,912 bytes。
- 空闲 600ms 的进程 CPU 时间增量为 46ms；这是短样本，不宣称空闲 CPU 为零或长期压力已完全验收。

core 最后修复包括已排队停止的 stopping 事件、退休 mailbox 与未确认请求末态兜底，macOS 的 SO_NOSIGPIPE 保护，以及 runner 活子进程启动超时与强制终止兜底。SO_NOSIGPIPE 仅经源码审查，未在 macOS 实测。资源数字是隔离样本，不是客户端全部组件固定上限或无泄漏证明。

## 复现入口与文件

```powershell
$env:PATH="$PWD/.tools/Qt/6.8.3/msvc2022_64/bin;$PWD/.tools/Qt/Tools/OpenSSLv3/Win_x64/bin;$env:PATH"
$env:QT_QPA_PLATFORM='offscreen'
$env:RABBITSHELL_TEST_REPORT_DIR="$PWD/test-results/0.1.11"
$env:PYTHONPATH="$PWD/.tools/test-python"
cmake -S . -B build -DRABBITSHELL_BUILD_TESTS=ON
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
& 'C:/Users/JOKER/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' tests/run_ports_integration.py build/Release/rabbitshell_ports_tests.exe
./scripts/package-windows.ps1 -Destination ./dist/RabbitShell-0.1.11
& 'C:/Users/JOKER/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' scripts/verify-package.py dist/RabbitShell-0.1.11 test-results/0.1.11
```

UI／集成改动：`src/ui/ports.*`、`workspace.*`、`workspace_tunnels.cpp`、`mainwindow.*`、`sshprompts.*`、`theme.*`、`icons.*`、`tests/test_ports.cpp`、`tests/run_ports_integration.py`、`CMakeLists.txt`、`README.md`、打包／验证脚本及本记录。core 代理维护 `src/core/tunnel.*`、共享 `sshtrust.h`、Session 信任辅助及核心隧道测试／fixture／runner；最终另经主审明确授权，仅接管 fixture relay 清理／堆栈诊断与对应 UI runner 证据，不修改生产 core。主审独占维护 SSH 方案与规范文档，最终版本随包复制。

macOS／Linux 未在对应系统构建或实测；Paramiko fixture 不替代 OpenSSH 策略验收或真实多平台验证。

## 新包部署与文件清单

独立新目录 `dist/RabbitShell-0.1.11`，61 个文件。EXE 1,641,472 bytes，与最终 `build/Release/RabbitShell.exe` 逐字节相同，SHA-256：

`FBCD0980043CAAF52C924C6590FC5895D0983975BDA409DA8551E2B9C5A7057B`

`package-verification.json`／`package-check.txt`：使用包 DLL／插件、仅包与 Windows 系统目录组成的 PATH、临时 CWD／data-dir、offscreen 及 CREATE_NO_WINDOW。`--version` 输出 `RabbitShell 0.1.11`、退出码 0；连接管理与 demo 各持续运行 1500ms、未提前退出，stderr 均为空，再由烟测程序结束。此检查验证部署启动；正常工作区等待隧道回收由前述真实 UI 用例验证。本轮没有重复无改动的包图标探针。

包内包含最新 README、本记录、主审最终 SSH 方案（第 10 节）、交互清单、通用 Qt 规范、历史版本记录、Seti／Microsoft 与其他依赖许可证，并附 `scripts/package-windows.ps1`、`scripts/verify-package.py` 供源码项目复现；这些开发脚本不是程序运行依赖。没有增加浏览器、Python 或远端程序的运行要求。

旧包 EXE 经核对保持：

- 0.1.10：`3693E48867B05B940E40FFEFE8C2F99C5DD1504AD59A4577C0C900641E017E5C`
- 0.1.9：`1B3FEF2267454E7CDA572908D840EB44DC6ACFE8E09062B0D23BBA1CE2521721`
- 0.1.8：`724567061FEDB853B7C7795064D130F6A7D670B3F43B1C40DD62125C4B2C7325`

最终 README／全部文档同步到包后生成 `test-results/0.1.11/package-manifest.json`，覆盖全部 61 个文件的大小与 SHA-256；交付摘要为同目录 `delivery-report.md`。构建日志为 `build-final.txt`，测试初始化修正的增量日志为 `build-tunnel-skip.txt`、`build-credentials-opt-in.txt`、`build-ports-final.txt`；生产源码快照摘要在 `source-hashes.json`。完整普通 CTest 的十份 QTest 报告已保存为同目录 `*-ctest.txt`，不依赖可能被后续运行覆盖的 build 报告。
