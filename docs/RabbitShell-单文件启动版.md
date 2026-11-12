# RabbitShell 0.1.12 单文件启动版

## 用法

只复制 `dist/RabbitShell-0.1.12-portable.exe` 到本地目录，双击启动；不需要旁置 Qt、Python、PowerShell、7zip 或 SDK，不安装、不请求管理员权限。支持中文和含空格的路径。

首次运行将完整旧目录包释放到：

```text
%LOCALAPPDATA%\RabbitShell\runtime\ede241d10887ea65598000b7c7a70f87e9fc3390a35759093e895b0a6a3037dd
```

后续运行逐文件验证并复用。缓存不是用户数据目录：默认继续使用原 RabbitShell 组织名、应用名及原配置位置，不会自动迁移或隔离既有配置。需要独立数据时，传入缓存以外的绝对路径：

```powershell
.\RabbitShell-0.1.12-portable.exe --demo
.\RabbitShell-0.1.12-portable.exe --data-dir 'D:\RabbitShell 数据' --demo
.\RabbitShell-0.1.12-portable.exe --version
```

启动器保留原始参数尾部，正确传递引号、空参数和尾部反斜杠；子进程 cwd 是运行缓存目录，因此相对数据路径也相对此目录解释。启动器等待子进程退出并转交退出码，重定向的 stdout/stderr 会传给子进程。

## 最终产物

| 项目 | 值 |
| --- | --- |
| EXE 大小 | 17,387,008 bytes（约 16.58 MiB） |
| EXE SHA-256 | `ED65A6E7AB2443958BB9AA7E311BFF2741D635EF90E3ED1C577BD963D0B26C11` |
| Payload SHA-256 | `ede241d10887ea65598000b7c7a70f87e9fc3390a35759093e895b0a6a3037dd` |
| 内部文件 | 62 个，共 34,584,228 bytes，与旧包逐文件一致 |
| 内部 RabbitShell.exe SHA-256 | `190196EA8A8673E8FEA3CFF300A5B845F8D7BE5DBF62C626AD120356DD2E6CCC` |

应用代码、根 CMake、现有资源、版本及旧目录包均不变。Qt 6.8.3 仍为动态链接；完整旧包的 `licenses/`、`NOTICE.md`、README 和文档均嵌入并释放，不以启动器替代许可证。启动器使用 C++20、原生 Win32、静态 CRT `/MT`，无 Qt 依赖、无控制台，静态导入仅 `KERNEL32.dll`；系统解压和 SHA-256 组件从 System32 加载。图标由既有紫色圆角底白色 `>_` 图案生成并嵌入，版本属性为 0.1.12。

## 缓存与安全边界

- 有界 UTF-8 清单记录每个文件的尺寸、压缩算法及 SHA-256；逐文件使用系统 XPRESS_HUFF 解压，处理后释放该文件内存，不常驻全量解包结果。
- 从盘符根到 managed root、子目录均以拒绝 write/delete 共享的目录句柄固定，拒绝 reparse/junction；目标文件也拒绝 reparse 和硬链接。只支持本地盘符绝对路径，不支持 UNC、设备路径或包含链接的目录链。
- 提取锁覆盖校验、修复和创建子进程，创建后释放，不覆盖等待应用退出的阶段，因此支持正常多开。
- **按最终用户授权，不使用逐文件或 marker 原子 rename。** 坏的已知文件先由已核对的句柄删除；随后对最终绝对目标 `CREATE_NEW + share0`，循环写入、Flush、关闭，再重新 SHA 校验并持有只读句柄。所有 payload 文件通过后，complete marker 同样独占写入并重新 SHA 校验；失败不启动。
- 锁阻止并发启动半包；中断遗留的 partial 文件和错误/缺失 marker 由下次全量验证修复。marker 从不替代完整文件验证；兼容清理旧 root `.rs-stage`。不递归删除目录，不清理别版缓存；未知内容导致拒绝启动，不擅自删除。
- 已验证的文件与目录句柄保持到子进程退出；遇到占用、只读损坏、空间不足等不能安全恢复的情况，停止而不是运行坏文件。
- 启动器限制自身启动前依赖搜索；子进程 cwd/DLL 目录固定在 runtime，PATH 仅含 runtime 与 System32，并过滤 Qt/QML 路径覆盖变量，避免从下载目录或 SDK 获取 DLL。

`RABBITSHELL_PORTABLE_RUNTIME=<绝对目录>` 是显式开发者覆盖：使用完整外部运行目录，不修复、不写缓存，仍拒绝链接、未知文件和缺文件；非 DLL 文件必须与原包一致，同名 DLL 可替换（非空且不超过 64 MiB）。这是对该目录 DLL 的主动信任，不是默认运行路径；可用于调试及动态库替换。

`RABBITSHELL_PORTABLE_NO_DIALOG=1` 禁止启动器 MessageBox，将错误写 UTF-8 stderr 并返回 125，同时对 child 使用隐藏启动及 `CREATE_NO_WINDOW`。该变量单独不保证 Qt 无窗口：自动化还必须设置 `QT_QPA_PLATFORM=offscreen`，隔离 LOCALAPPDATA/APPDATA/数据目录，并使用专属 Job；不要把普通用户的正常启动当作隔离测试。

## 构建与验证命令

以下为重现命令记录，本次最终收尾没有再次执行测试或构建：

```powershell
.\scripts\package-portable-windows.ps1
py -3 scripts/verify-portable.py --static-only
py -3 scripts/verify-portable.py --run-isolated
```

构建依赖 Python ≥ 3.10、CMake、MSVC 和 Windows SDK；本机为 Python 3.11.9、CMake 4.2.1、VS 18 2026 / v143 14.44.35207、Windows SDK 10.0.26100.0。`launcher/CMakeLists.txt` 独立于应用根配置，生成与编译结果限定在 `build/portable-0.1.12/`。打包不重新编译应用、不改旧包，也不自动执行应用。

主要源码为 `launcher/launcher.cpp`、资源模板和 manifest；打包脚本为 `scripts/package-portable-windows.ps1`、`scripts/make-portable-payload.py`、`scripts/portable_format.py`；隔离验证为 `scripts/verify-portable.py`、`scripts/portable_isolation.py`，另有独立 child 参数探针及其编译脚本。参数探针不进入正式发行包，不替代真实 Qt 测试。

此前同配置、不同生成/编译目录的中间版本曾字节一致；其 hash 不是最终 EXE。遵照最终指令，最终版本没有再次运行 repro，不将历史结论套用于最终包。

## 实测结果与限制

2026-11-04 本机隔离包装验证 **PASS：27 cases，0 skip**。包含真实 `--version` 输出 `RabbitShell 0.1.12` 和退出码 0、中文空格路径、首次提取/复用、4 路并发、损坏/缺失/未完成缓存恢复、拒绝未知内容、只读失败、8 个 junction 场景、硬链接和文件 symlink 拒绝、参数原文及 argv 探针、开发者覆盖。每项下载目录始终只有复制的单 EXE，Job 结束后剩余进程为 0；哨兵及其他 payload 未改。

真实 Qt manager/demo 在同一缓存同时保活，Qt 模块来自缓存；退出前的 `alive_samples` 独立保存。测试仅结束自己 Job 中的 child，使启动器转交 **73**：这是测试主动终止，不是正常 UI 关闭测试。

| 观察项 | 实测值 |
| --- | --- |
| 新 runtime cache 的首启 wall clock | 1055.539 ms |
| 同 cache 验证复用 wall clock | 82.806 ms |
| launcher Private Bytes：manager 保活时 | 1,036,288 bytes |
| launcher Private Bytes：demo 保活时 | 1,040,384 bytes |

首启/复用差值约 972.733 ms，包含 Qt 启动、磁盘与轮询成本，不是纯解压 CPU 时间；“首次”指 payload cache 新建，不保证操作系统磁盘缓存冷态。启动器主动 trim 工作集，trim 后 Working Set 不能当作真实内存占用，也不能代替 Private Bytes；表中是短时样本，不是长期性能或泄漏证明。

所有运行使用最小 PATH、临时配置目录、独立 `--data-dir`、offscreen、NO_DIALOG 与专属 Job。未连接真实服务器、读取凭据、操控可见桌面或运行完整 SSH/UI 回归；不声明可见桌面交互已验收。包未签名，仍可能受 SmartScreen/安全软件策略影响；不同 Windows/文件系统/压缩实现和工具链未构建矩阵验证。

报告：[最终索引](../test-results/portable-0.1.12/final-report.json)、[27 项原始 PASS 及保活样本](../test-results/portable-0.1.12/isolated-20261104-164205-3b6ba409/results.json)、[嵌入包静态核对](../test-results/portable-0.1.12/static-report.json)、[构建信息](../test-results/portable-0.1.12/build-report.json)、[历史中间版本 repro](../test-results/portable-0.1.12/repro-report.json)。全部失败夹与日志保留；其中早期 rename 试验失败夹内出现的 `NOTICE.md` 也未删除，最终实现不再使用该路径方案。
