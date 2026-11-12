# RabbitShell 0.1.14 验证记录

日期：2026-11-05。Windows x64 Release，Qt 6.8.3、MSVC、OpenSSL 3.0.16、libssh2 1.11.1。

0.1.13 将 list-unit-files 返回的 `apport-coredump-hook@.service` 等模板直接加入 systemctl show 批次。模板没有具体实例名，systemctl 因此返回 `Failed to get properties: Unit name apport-coredump-hook@.service is neither a valid invocation ID nor unit name.`，整个服务页失败。本版区分合法模板与具体实例，不移除模板、不虚构默认实例。

模板保留列表中的真实自启状态；运行状态明确为“模板（需指定实例）”，没有伪造的 ActiveState/SubState。模板不参与 show，行菜单仅允许已知详情/复制名称，其余控制、自启修改、日志与删除禁用并说明需选择具体实例。详情直接使用列表已知名称/自启信息，不再触发失败查询。后端也拒绝直接模板控制、日志、删除和创建共享模板。`apport-coredump-hook@worker.service` 等实际实例仍正常查询和控制，实例删除限制沿用 0.1.13。

局部容错只用于列表读取：批量 show 非零退出时保留已返回的完整具体服务属性块；单项消失、not-found、不完整或首个失败后的未读项标为“未知 / 读取失败”，保留已知文件自启信息，并在页内和 tooltip 显示原始原因。没有逐项回退或重试，批次最多 96、服务最多 5000；首次失败后未读取的其他项可以保持未知，等待下一次显式/周期刷新。全部具体 unit 都无可用属性时仍失败；manager 探测、枚举、整体权限拒绝和 transport unknown 不伪装成功。既有写删步骤仍失败即停，不重放变更。

## 复现与验证

全部运行使用临时配置、`QT_QPA_PLATFORM=offscreen`、隐藏进程及随机 loopback SSH fixture。未连接真实服务器、读取真实凭据或 known_hosts、调用真实 systemctl 或操作可见桌面。

先只更新严格 fixture/测试，沿用尚未修复的后端构建运行 red：**3 passed / 1 failed / 0 skipped**（含 init/cleanup）。失败正是用户截图的模板错误，证据为 `test-results/0.1.14/red/services-integration.txt` 及命令审计。fixture 在首个失败 unit 立即停止返回后续属性，仅保留之前成功块；vanished 不伪造 not-found 成功块。此 red 是旧算法的真实 SSH 协议失败，未更改或运行旧 dist 包。

修复后完整服务专项 **17 passed / 0 failed / 0 skipped**，报告 `services-integration.txt`。新增覆盖：

- 合法模板保留和真实 static 状态、模板无 show 请求、实际实例运行状态和 restart。
- 绕过 UI 的模板 start/stop/restart/reload/enable/disable/logs/删除拒绝，创建共享模板在远端请求前拒绝；模板详情零远端请求。
- 严格 SSH fixture 的单项消失、整体查询权限拒绝、实际 manager 不可用。
- mock 的失败位于批次中间、成功返回但属性不完整/not-found、后续未读项不伪造 inactive、全部失败、部分有效输出后 transport unknown 仍整体失败；每次固定 4 个只读请求，没有重试。
- 真实生产 UI 的模板菜单禁用/说明、本地详情、已停止/运行筛选不误收模板或未知项、局部 warning 显示与恢复。
- 保留 0.1.13 必要生成/注入、写删失败即停、认证代次、生命周期、文件选择、输出上限/断线/超时回归。

最终命令/stdin 审计为 **50 请求、1 创建、1 删除**，所有模板控制均未产生远端请求，实例实际收到查询和重启命令。fixture 从不执行 shell 或 systemctl；mock 的错误注入也不能替代真实系统验收。

受影响 UI 定向 CTest 为 **2 个目标通过、0 目标失败**：workspace **16/0/0**，ports **9/0/2**。ports 的 2 个真实网络 fixture 用例因本轮未提供该 fixture 明确跳过；没有把它们记录为通过。报告 `ctest-ui.txt`、`workspace-Release.txt`、`ports-Release.txt`。本版未重做全套 CTest、SSH 矩阵、内存压力或旧 portable 27 项。

已查看 `template-workspace-dark.png` / `template-workspace-light.png`：使用实际 Workspace、三页共用头部、真实字体，模板/具体实例/未知项和 warning 均可辨；可拖列宽保留，文本省略时 tooltip 显示完整信息。Qt offscreen 的字体目录、raise/键盘抓取等平台警告保留；不据此声称可见桌面/DPI 验收。

## 构建与交付

修改范围：`src/core/services.{h,cpp}` 的模板识别/后端拒绝/列表只读容错，`src/ui/services.cpp` 的模板详情/菜单限制和 warning，严格 fixture/服务测试，CMake 版本、README、服务实现/检查清单及打包文档列表。Session 认证/传输与 `launcher/launcher.cpp` 核心不改。

构建入口：`cmake -S . -B build`，`cmake --build build --config Release --target RabbitShell rabbitshell_services_tests rabbitshell_workspace_tests rabbitshell_ports_tests --parallel 4`。测试先配置 Qt/OpenSSL 的进程 PATH 和 `.tools/test-python` 的 PYTHONPATH，再用 Python 3.12 执行 `tests/run_services_integration.py build/Release/rabbitshell_services_tests.exe`；UI 为 `ctest --test-dir build -C Release -R "^(workspace|ports)$" --output-on-failure`。

新目录包为 `dist/RabbitShell-0.1.14`；新单 EXE 为 `dist/RabbitShell-0.1.14-portable.exe`。现存 0.1.12 / 0.1.13 两个目录和两个 portable 共 129 个文件作为只读基线保留。文档完成并同步到新包后再生成固定 inventory，使用现有参数化脚本 `package-portable-windows.ps1 -Version 0.1.14 -Manifest <inventory.json>` 打包；不调用旧默认输出，也不改 launcher 核心的 pins、锁、CREATE_NEW/Flush/hash 路径。

最终静态、目录包与单 EXE 的 --version/manager/demo 隔离烟测、尺寸/hash/内部 app hash、源文件清单及旧包复核分别记在 `test-results/0.1.14/final-report.json` 与 package/portable 报告中。文档在打包前冻结，最终报告在包外生成以避免哈希自引用。

限制：未验证真实 systemd、具体发行版/polkit/ACL/LSM、真实服务动作及 macOS/Linux 客户端。本次修复基于模板语义和严格协议 fixture，不宣布这些真实环境通过；0.1.13 的创建/删除权限与来源保守边界继续适用。
