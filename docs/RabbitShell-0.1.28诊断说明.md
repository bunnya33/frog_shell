# RabbitShell 0.1.28 本机异常诊断

2026-11-10。此前用户报告的一次 0.1.27 闪退根因仍未知：最后会话约 217585ms，末尾完整 MonitorUiEnd，无 Stopped/UiStall；主审未发现相应 Windows 1000/1001/1002、WER 或 dump。此信息不足以判定原因，本版不宣称修复该闪退。观察证据保留于包外 `test-results/0.1.28/crash-investigation/observation.json`。

Windows 主程序在启动诊断时预开一个异常记录槽，最多 16 个 `logs/crash-00.log` 至 `crash-15.log`，优先空槽、否则轮换旧槽。单条固定缓冲不超过 512 字节，只记录事件、版本、UTC、进程/线程号，以及可取得的异常码、模块基础名和模块内偏移；不记录内存转储、正文、命令、凭据、原始路径或原始错误文本。致命路径不调用 Qt 日志/锁或分配内存；停止诊断会恢复已有异常及 terminate handler。正常启动可能产生空槽，空槽不代表异常。

未处理 SEH 和 std::terminate 的捕获为最佳努力。显式 abort、fast-fail、外部 TerminateProcess、安装 hook 前的异常、严重栈/堆损坏、目录不可写或全部槽被占用时可能没有记录；模块定位和写盘也可能失败。记录缺失不能证明正常退出，模块偏移不等于已定位根因。既有有界阶段日志、后台写入和 UI 停顿检测继续保留。

portable 启动器等待子进程后，为非零退出码尽力写一条 `logs/exit-00.log` 至 `exit-15.log`，最多 16 槽。字段仅 ChildExit、版本、UTC、launcherPid、childPid、exitCode；不改变启动流程或退出码。ChildExit 表示非零退出，不能自动等同 crash；隔离烟测主动终止子进程所用 73 也只记录 ChildExit。启动器不建立管道，静态导入仍只含 Kernel32，命令行解析动态加载 System32 的 shell32。

启动器记录位置优先使用命令行最后一个明确的 `--data-dir <绝对目录>` 或 `--data-dir=<绝对目录>`，无该选项时使用 `RABBITSHELL_DATA_DIR`，再回落本机应用数据目录；`--` 后不继续解析。相对路径、UNC、歧义/不支持的参数形式、重解析路径或落入解压 runtime 的路径会跳过此可选记录，不擅自换目录。它不会扫描服务器，也不读取用户文件内容。测试使用私有 CLI/env/default 目录，核对不误写其他位置。

异常用例由独立 offscreen 子进程触发，用合成异常码验证 schema、轮换、路径失败和 handler 恢复，不是用户闪退复现。生产环境是否留下记录仍受以上边界限制。所有诊断只发生在本机，不创建远端 helper、恢复文件或备份。
