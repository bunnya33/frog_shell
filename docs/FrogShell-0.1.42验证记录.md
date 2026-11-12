# FrogShell 0.1.42 验证记录

本轮新增关于窗口、GitHub Release 检查与下载、Windows 单 EXE 重启更新及对应发布工作流。0.1.41 的包与证据继续冻结。主线程已完成最终独立验收并授权封版；源码冻结后生成目录包和单 EXE，包哈希与隔离 smoke 结果单独写入 `test-results/0.1.42/final-report.json`，不再回写冻结文档。

## 已运行的隔离回归

- `release_update`：42 通过、0 失败。覆盖版本/资源/摘要/URL边界、HTTP/TLS/超时/取消、有界响应、流式下载以及重入回调失效。
- `self_update`：24 通过、0 失败。仅临时本地 NTFS 文件与合成进程，覆盖等待真实进程退出、取消、身份变化、占用、回滚、真实 helper 与合成新 launcher 的启动确认、未确认时保留备份以及 owned helper 退出。生产应用不包含 fixture 专用入口。
- `rabbitshell_updateui_tests`：13 通过、0 失败。包括未配置零请求、单例、输入校验、纯文本发布说明、关闭关于后下载继续、目录包手动提示、下载取消、AppLock、菜单与退出取消。额外以真实合成父进程验证生产 prepareInstall 的准备中取消、保存失败/成功、确认期间内容变化、迁移互斥和退出取消。测试不启动安装 helper，不替换运行中的应用。
- 原生 launcher origin fixture：2 项通过。正式模式的路径、父 PID、创建时间、文件身份与 SHA-256 均来自 launcher 自身；开发 runtime 覆盖模式不传 origin，两模式都过滤继承的 PORTABLE 变量。
- 应用菜单暗浅定向回归：4 通过、0 失败。原夹具仍按六个顶层菜单断言，新增帮助后已按七个菜单及帮助标题更新；旧 2/2 记录保留为 `worker-menu-final/first-red-six-menus.txt`，未因此改动产品。
- 打包参数回归 2/0，两个 PowerShell 打包脚本静态语法检查通过。TLS 包装保留 Qt Schannel 后端，不新增 libssl 依赖。

初次扩展周期测试把“已有可用更新”误期待为节流状态，记录为 12/1 并保留。最终分别验证已有结果稳定、新控制器读取持久化时间后进入节流，13/0 记录为准。

## 证据位置

`test-results/0.1.42/worker-update-first/` 保留初轮三个目标报告；`worker-update-final/ui-tests.txt` 是扩展 UI 最终报告，`ui-evidence` 含暗浅截图和每个独立退出子进程报告。`worker-launcher-origin/origin-tests.json` 为原生身份观测记录。匹配源码和静态库检查点在 `worker-ui-checkpoint-2/`，包含 manifest；它不代表发布冻结。

主线程独立 UI checkpoint1、checkpoint2 均为 8/0，已人工核对暗浅及下载完成截图，确认底栏按钮有常态边框。主线程的 `main-*` 文件由其独立维护。

## 适用边界

没有配置或请求真实 GitHub 仓库，没有发布 Release，没有接触真实服务器、凭据或系统剪贴板。Windows 自动安装只验证本地 NTFS/x64；其它平台和目录包保留手动更新。默认不记录日志，更新日志仍服从全局开关和目录。CI 工作流仅本地定义与静态审查，尚未在 GitHub 实际执行。冻结清单和 ZIP 显式包含隐藏目录 `.github/workflows/release-windows.yml`；包级报告记录部署字节、图标和启动验证。
