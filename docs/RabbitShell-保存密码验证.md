# 保存密码验证记录（0.1.2）

2026-11-03，Windows 11 x64、Qt 6.8.3、MSVC v143、Release。依据 [设计与验收约定](RabbitShell-保存密码设计.md)。测试只读取随机临时目录中的合成连接配置，不读取真实 `connections.json`，不连接真实服务器，不操作已有用户窗口。原有 0.1.1 目录和进程保留。

## 实现与依赖

- Windows：原生 Credential Manager，链接系统 `advapi32`，无新增常驻服务或第三方运行库。
- macOS：Keychain / Security.framework。Linux：CMake 检测可选的 `libsecret-1`；缺失时普通连接仍可用，保存功能明确报错，无明文回退。GLib 头文件局部屏蔽 Qt `signals` 宏。两者仅完成源码适配，**未在相应平台构建或实机验证**。
- 平台库读、写、删在后台串行队列执行；Linux 使用 GCancellable 的约 15 秒取消期限。返回 found / missing / error 三态，空私钥口令可以被保存、读取为 found。
- 系统条目标识是应用专属前缀与配置 ID、host、user、port、auth、适用时 key path 的 SHA-256。普通 JSON 只存偏好；复制、导出、导入新副本不继承保存设置。
- 同认证身份更新凭据用于后续重连和传输；身份改变要求先正常关闭旧工作区，编辑草稿和任务保持原服务器归属。修订号防止撤销保存、删除或新提交后，旧认证结果重新写回。
- 表单保存可单独完成；运行时凭据在认证成功后写一次。已有凭据读取与普通传输认证不重复写入。保存失败提示一次，本次会话继续工作。
- 仅服务端密码认证拒绝触发密码候选失效与重输；有效存储不会因网络、握手或指纹问题删除。私钥认证失败提供明确的“重新输入口令／取消后检查私钥”入口，不将文件读取失败直接判为口令错误。
- 存储变更失败保留配置原状态，配置写入失败尝试回滚系统库；回滚失败明确提示。保存中禁用 Esc/系统关闭；主窗正常退出异步等待提交完成，保持响应。不宣称强制结束/断电时具有跨存储原子事务。

## 验收对应

| 场景 | Windows 自动验证 |
| --- | --- |
| 默认不保存，关闭工作区再打开 | 再弹输入框，系统库存储次数为 0 |
| 记住密码，不传 secret 断开重连 | 同一工作区自动读取，登录弹窗计数不增加，草稿保留 |
| 关闭工作区后重新打开 | 自动读取，登录弹窗计数不增加 |
| 退出／另进程读取并登录 | 真实 Windows 系统库存取，子进程读取并向本机 fixture 认证成功 |
| 名称／目录变化且密码留空 | 系统库原凭据保留 |
| 身份变化 | 独立标识；新身份不得沿用旧密码，旧工作区与编辑草稿不重绑定 |
| 取消保存／删除配置／晚到认证 | 删除条目；过期请求回滚，不能恢复已撤销策略或已删除配置 |
| 错误保存密码 | 一次重输后成功更新，没有自动重试循环 |
| 加密私钥错误口令 | 提供重输入口，使用正确口令成功认证并更新；空口令有独立状态 |
| 主连接与传输连接握手失败 | 内存候选及系统库均保留，不弹密码重输 |
| 指纹拒绝／变化 | 协议测试无 passwordRejected 信号 |
| 同身份新密码更新既有断开工作区 | 使用新密码，工作区数量保持 1，草稿保留 |
| 认证后独立传输 | 本机 SFTP 下载完成；普通传输认证不额外保存密码 |
| 写入失败后传输 | 提示一次，当前会话和传输仍工作，无重复写入／警告 |
| 复制／导出 | 不含密码、不含保存偏好；配置无合成密码明文 |
| 系统库读写删失败／配置写失败 | 注入失败验证原状态、反馈及回滚；无明文回退 |
| 保存中 Esc／系统关闭／应用退出 | 表单不能半途取消；应用等待存储完成，事件循环仍响应 |

## 测试结果与复现

本轮本机 fixture 实际运行：`CredentialTests` **13 passed / 0 failed / 0 skipped**；`SessionTests` **3 passed / 0 failed / 0 skipped**；原有 `CoreTests` **9 passed / 0 failed / 0 skipped**，保留编辑器 QStringDecoder 生命周期回归。合计三套测试 **25 passed / 0 failed / 0 skipped**（包含每套 init/cleanup；实际 19 个测试槽，不重复累加 CTest 与 fixture 的同一用例）。未提供 fixture 端口时 CTest 会明确跳过网络场景，不能用该次结果替代实际协议联合测试。

Windows 原生库测试要求实际通过，不以 skip 掩盖失败。Linux 无 libsecret 或 CI 无可用密钥环时，原生系统库用例明确 skip 并给原因；可注入后端和普通连接测试仍运行。该分支未在本轮 Linux 实机执行。

```powershell
cmake --build build --config Release --parallel 8
$env:PATH = "$PWD/.tools/Qt/6.8.3/msvc2022_64/bin;$PWD/.tools/Qt/Tools/OpenSSLv3/Win_x64/bin;" + $env:PATH
ctest --test-dir build -C Release --output-on-failure
$env:PYTHONPATH = "$PWD/.tools/test-python"
$env:RABBITSHELL_TEST_REPORT_DIR = "$PWD/test-results/0.1.2"
& 'C:/Users/JOKER/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' tests/run_integration.py build/Release/rabbitshell_session_tests.exe
& 'C:/Users/JOKER/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' tests/run_integration.py build/Release/rabbitshell_credential_tests.exe
.\scripts\package-windows.ps1 -Destination "$PWD/dist/RabbitShell-0.1.2"
```

稳定报告：`build/core-Release.txt`、`build/credentials-Release.txt`、`test-results/0.1.2/rabbitshell_session_tests.txt`、`test-results/0.1.2/rabbitshell_credential_tests.txt`。打包目录 `dist/RabbitShell-0.1.2` 带设计与验证文档及含 exe SHA-256 的 `build-info.json`。

打包后将该子进程 PATH 缩至 Windows 系统目录、清空 QT_PLUGIN_PATH，隐藏执行包内 `RabbitShell.exe --version`：退出码 0、输出 `RabbitShell 0.1.2`、stderr 为空，验证包内依赖及版本入口。该命令不创建工作区、不读取连接配置。未做干净机器完整 GUI 启动验收。

主要修改：`src/core/credentials.*`、`profile.*`、`session.*`，`src/ui/mainwindow.*`、`workspace.*`，`CMakeLists.txt`、`tests/test_credentials.cpp`、`test_session.cpp`、`ssh_fixture.py`、`run_integration.py`、`scripts/package-windows.ps1`、README 和本实现／验证文档。`Editor::reload` 的 payload 生命周期修复保持不变。无 commit，原有 docs/prototype 未清理。

Qt offscreen 测试的字体目录与 `propagateSizeHints` 提示属于测试平台插件提示。本轮没有以 offscreen 自动测试替代真实桌面主题、各 DPI 或干净机器验收；也不将此功能的通过表述为完整原型全部完成。
