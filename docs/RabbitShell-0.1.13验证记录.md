# RabbitShell 0.1.13 验证记录

日期：2026-11-05。Windows x64，MSVC / Qt 6.8.3 / OpenSSL 3.0.16 / libssh2 1.11.1，Release。

范围：只使用临时配置、QT_QPA_PLATFORM=offscreen、隐藏进程和本机 loopback SSH fixture；没有连接真实服务器、读取真实凭据、改真实 known_hosts、运行真实 systemctl 或操作可见桌面。真实 systemd、真实 polkit/ACL、安全策略和 macOS/Linux 客户端仍需验收。

服务专项最终 **13 passed / 0 failed / 0 skipped**，报告 test-results/0.1.13/services-integration.txt。覆盖独立配置生成/解析、换行注入、中文空格/引号/反斜杠/%/$、首个 executable 限制、四种自启/启动组合、同名冲突、部分写入成功、权限失败、未知结果不重放、删除来源拒绝/文件变化/分步中断、UI 身份/排序/隐藏刷新/销毁、确认时刷新、内存凭据后端的保存/取消保存/过期认证、UID 畸形拒绝、查询恢复、command-only 与 SFTP 拒绝、远端导航/取消、工作区服务独立连接/凭据失效/退出、真实 SSH 通道断线/输出超限/15 秒超时。写删命令和 stdin 审计为 38 次请求、1 次创建和 1 次删除；fixture 不运行 shell。复杂权限/文件来源失败由模拟 runner 验证，不冒充真实文件系统安全验收。

首轮 10/1/0：确认测试在 showEvent 先发查询后才设置假连接状态，导致动作未进入确认；调整测试的显示时机，保持原断言。最初截图未加载字体，已加载系统 Microsoft YaHei / Consolas 后重生成；最新 workspace-services-dark/light 和 services-dark/light-narrow 是真实生产控件离屏渲染，已查看文字、三页共用头部、窄面板和圆角。保留初次失败报告 services-initial-failure.txt。字体目录和 offscreen propagateSizeHints 的 Qt 警告保留，不等同于可见桌面验收。

完整 CTest 一轮：12 个目标中 **10 通过、2 跳过、0 失败**；ssh_sftp/sshtrust 因没有提供网络 fixture 跳过，各套件内部的网络/真实系统凭据测试 skip 单独保留。末尾两个服务小修（查询错误恢复、减少列表属性/移除非必需 --value）之后只定向重建并复测服务目标，没有重复整套无关压力测试。

独立 loopback 回归：session SSH/SFTP/PTTY **3/0/0**；known_hosts ECDSA fixture 覆盖 LF/CRLF、变化、坏格式和确认竞争 **36/0/0**；两个受影响端口/工作区集成 **4/0/0**，其审计 active_transports/active_relays 最终为 0。它们分别保存在 session/、trust/、ports/。没有重跑隧道压力矩阵、内存压力或旧 portable 27 项。

包装参数保护测试 tests/test_portable_parameters.py：**1/0**，检查新版本必须提供 pinned manifest、旧默认 pins、不匹配版本/内容拒绝、任意受限文件数量和 payload hash。launcher.cpp 执行/提取核心不修改，继续保留目录/文件强 pins、锁内 CREATE_NEW、Flush、hash 后启动；没有恢复 atomic rename。

0.1.13 目录包与单 EXE 使用新的输出路径；旧 dist/RabbitShell-0.1.12 和 0.1.12-portable.exe 为只读基线，初始 63 个文件记录在 old-packages-before.json。已不存在的历史版本不伪造校验通过。

目录包启动、单 EXE 的实际 --version/manager/demo 烟测、最终尺寸/hash/内部 app hash、旧包复核及源文件清单由交付时生成的 test-results/0.1.13/final-report.json 和各 package/portable 报告给出。本文不把尚未完成的打包过程写成已通过；读取交付报告确认最终结果。

复现：先配置 Qt/OpenSSL PATH 和 .tools/test-python 的 PYTHONPATH，再构建 Release；ctest --test-dir build -C Release；使用 Python 3.12 运行 tests/run_services_integration.py build/Release/rabbitshell_services_tests.exe。目录包用 scripts/package-windows.ps1 -Destination ./dist/RabbitShell-0.1.13。单 EXE 用 scripts/package-portable-windows.ps1 -Version 0.1.13 -Manifest <核对的inventory.json>；默认参数仍为旧 0.1.12 流程，不应为重建新版本调用旧默认输出。
