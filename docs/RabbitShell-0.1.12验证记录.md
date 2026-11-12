# RabbitShell 0.1.12 验证记录

本轮只修复共享 SSH 主机身份校验的 Windows 换行兼容回归，以及将校验错误误报为身份变化的问题。平台为 Windows x64、Qt 6.8.3、MSVC Release。自动化测试全部使用临时配置和隔离 loopback SSH fixture，未读取或修改用户真实 known_hosts。主线程诊断另对真实应用 known_hosts 做了只读的公钥指纹、CRLF 与前后文件哈希检查，未写入，最终哈希保持不变。本轮未读取真实凭据、连接实际服务器或操控可见桌面。文档不记录用户服务器地址、公钥或指纹。

## 问题与修复

旧版 Windows 文本模式写出的 known_hosts 使用 CRLF。0.1.11 共享 helper 改用 QFile 二进制 readLine 后，CR 会保留；libssh2 1.11.1 的 knownhost_readline 只扣除 LF，hostline 的密钥字段以空格或 Tab 分隔，无尾随注释时 CR 被包含在 Base64 密钥文本内。knownhost_check 比较 Base64 文本，因此同一真实密钥也会得到不匹配。

- 读取时只去掉一个末尾 LF 及其紧前的 CR；保留无末尾换行的正常记录，不 trim 整条记录或密钥。空白／注释行只识别空格与 Tab；内部 CR、NUL、其他非法控制字符和裸 CR 均报错，不清洗成可用密钥。
- 保留 4 MiB 文件限制、16 KiB 原始行限制、进程内互斥、保存锁和 QSaveFile 原子提交；增加累计读取量检查及具体失败原因。发现坏记录即拒绝校验，即使文件中已经出现匹配记录也继续检查完整文件。
- Result 增加 reason；普通 Session 与 Tunnel 的 Error 分支仅报告“主机身份校验失败”及原因，不发出 hostKey(changed=true)。实际 Changed 仍通知身份变化并硬拒绝，不修改现有信任记录。
- 用户确认未知主机后重新读取与校验；期间若文件被换成其他密钥则报告变化，若被损坏则报告校验失败，均不覆盖新文件。不更改 hostKey 信号签名，没有自动重置信任或绕过入口。
- 已信任文件只读连接时逐字节保持原样。仅明确接受未知主机时沿用原子序列化保存，保留其他主机密钥条目；新保存使用 LF。

生产修改仅为 `src/core/sshtrust.h`、`src/core/session.cpp`、`src/core/tunnel.cpp`；另更新版本、测试、README、打包与本记录。没有调整主题、布局或引入新远端依赖。SSH 方案第 11 节由主审维护，最终版本随包复制。

## 先复现再修复

`test-results/0.1.12/reproduction/sshtrust-tests.txt` 为修复生产代码前的有效隔离复现：**13 passed、23 failed、0 skipped**。Session 和 Tunnel 的 LF、无末尾换行样本通过；同一 ECDSA 密钥的“无注释、直接 CRLF”样本均未认证，混合换行样本也失败。真实不同密钥样本仍拒绝。坏文件的错误变化提示与确认竞态差异也被测试捕获。

首次测试草稿每次构造 Profile 生成不同 id，导致隧道规则身份和服务身份不一致，未到达主机校验。主审指出后已共用一次构造的 Profile；该无效运行被终止并单独保留在 `invalid-test-profile-initial`，不作为 Tunnel 复现或产品缺陷证据。有效复现报告在修正测试后生成。早期测试 EXE 尚运行造成一次重链失败，停止隔离测试进程后重建成功，没有修改其他应用进程。

## 定向信任协议测试

新建 `tests/test_sshtrust.cpp` 与 `tests/run_sshtrust_integration.py`。共享 fixture 增加可选 ECDSA P-256 测试主机密钥，默认 RSA 行为不变；公钥仅来自新建的隔离 fixture。普通 Session 通过 authenticated 后实际申请 SFTP，由此 fixture 明确拒绝；该拒绝用于结束普通连接，不能当作完整 SFTP 成功证据。完整 SSH/SFTP 的成功路径由后面的既有 Session 测试独立验证。

| 隔离密钥 | passed | failed | skipped | 报告 |
| --- | ---: | ---: | ---: | --- |
| ECDSA P-256 | 36 | 0 | 0 | `ecdsa/sshtrust-tests.txt` |
| RSA | 36 | 0 | 0 | `rsa/sshtrust-tests.txt` |

数量包含 init／cleanup。每套覆盖两种传输路径：LF、CRLF 无注释、无末尾换行、混合换行与空白注释；三种换行的真实密钥变化拒绝；坏格式、内部 CR、裸 CR、内部 NUL、超长行、超大文件、合法记录后的坏尾行；首次信任保存后用另一种传输重读且保留其他主机记录；确认期间文件被换成不同密钥或坏格式。

对应 `sshtrust-audit.json` 每套均为 36 次隔离 SSH 连接、12 次认证、6 次显式 SFTP 拒绝；exec／PTY／shell／direct-tcpip 为 0，最终 active_transports／active_relays 为 0。保存失败及其他 I/O 分支提供具体错误文本，但没有宣称本轮对所有系统权限／磁盘故障做过注入验证。

## 必要回归

本轮最终 build 后运行 11 个常规 CTest target：**9 passed、2 skipped、0 failed**，15.32 秒，见 `ctest-final.txt`。ssh_sftp 与 sshtrust 因普通 CTest 未提供 fixture 而整项跳过；credentials／tunnels／ports 内需 fixture 的项和系统凭据库 opt-in 项也按原规则跳过，不能计为协议通过。11 份 `*-Release.txt` 为本轮报告。

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
| sshtrust | 0 | 0 | 1 |

受影响的独立协议／集成回归均零跳过：

- `session-protocol.txt`／`session/rabbitshell_session_tests.txt`：**3/0/0**，完整 SSH、SFTP、文件冲突保护、传输与 PTY 既有用例。
- `tunnel-protocol.txt`／`tunnel/tunnel-tests.txt`：**14/0/0**，选取 12 个相关用例及 init／cleanup，覆盖真实转发、双向多规则、两向半关闭、本地端口占用、服务端拒绝、信任取消／变化／确认竞态、密码拒绝、私钥认证、握手取消／期限、独立规则取消、退休边界、传输中断及实际销毁。没有重做并发限额、慢读内存或反复生命周期压力测量。
- `ports-protocol.txt`／`ports/ports-ui-tests.txt`：**4/0/0**，选取单规则应用重启／取消与 SFTP 失败时独立转发两个相关用例及 init／cleanup；没有重做主题截图。

纯 Tunnel 审计为 27 次 SSH 连接、22 次 direct-tcpip，exec／SFTP／PTY／shell 均为 0。端口集成为 6 次连接、8 次 direct-tcpip、SFTP 恰好 1 次，exec／PTY／shell 均为 0。两者最终 active_transports／active_relays 均为 0。故意断开场景的传输错误有保留，清理异常列表为空，不据此宣称长期压力或所有平台已验收。

## 复现入口

```powershell
$env:PATH="$PWD/.tools/Qt/6.8.3/msvc2022_64/bin;$PWD/.tools/Qt/Tools/OpenSSLv3/Win_x64/bin;$env:PATH"
$env:PYTHONPATH="$PWD/.tools/test-python"
$env:QT_QPA_PLATFORM='offscreen'
$env:RABBITSHELL_TEST_REPORT_DIR="$PWD/test-results/0.1.12/ecdsa"
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
& 'C:/Users/JOKER/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' tests/run_sshtrust_integration.py build/Release/rabbitshell_sshtrust_tests.exe --hostkey-type ecdsa
# RSA 复测时将报告目录改为 rsa，并指定 --hostkey-type rsa。
./scripts/package-windows.ps1 -Destination ./dist/RabbitShell-0.1.12
& 'C:/Users/JOKER/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' scripts/verify-package.py dist/RabbitShell-0.1.12 test-results/0.1.12
```

## 部署与一致性

Windows 独立新包 `dist/RabbitShell-0.1.12`。最终构建 EXE 1,646,080 bytes，SHA-256：

`190196EA8A8673E8FEA3CFF300A5B845F8D7BE5DBF62C626AD120356DD2E6CCC`

部署与一致性检查已完成：

- 新包共 62 个文件，EXE 与最终 build 的大小和 SHA-256 完全一致。`--version` 输出 `RabbitShell 0.1.12`，退出码 0。
- 最小 PATH 仅含包目录与 Windows 系统目录，使用包内 DLL／插件、临时 CWD／数据目录、offscreen 与隐藏进程。manager／demo 均运行 1500ms 未提前退出，stderr 均为空，之后由烟测程序结束；见 `package-verification.json`。没有重新运行主题截图、无关压力测试或图标探针。
- 包内 README、文档及脚本共 24 项与源文件逐字节相同，包含主审最终 SSH 方案第 11 节与本版本验证记录。最终文档同步后重新生成全部 62 文件的 `package-manifest.json`，并通过 `final-consistency.json` 核验。
- 旧版 0.1.4 至 0.1.11 的 8 个目录共 407 个文件与本轮打包前快照比较，文件集合、大小及 SHA-256 全部保持不变。0.1.11 EXE 仍为 `FBCD0980043CAAF52C924C6590FC5895D0983975BDA409DA8551E2B9C5A7057B`；见 `old-packages-verification.json`。

构建记录为 `build-final.txt`，包部署记录为 `package.txt`／`package-check.txt`，最终生产与测试源码摘要为 `source-hashes.json`。这些证据统一位于 `test-results/0.1.12`。

本轮只有 Windows／Paramiko loopback 实测；没有使用真实服务器验证，也没有重复 macOS／Linux 实机或 OpenSSH 策略验收。新版本直接兼容既有 CRLF 记录，修复不要求清空或重新接受已有主机信任。
