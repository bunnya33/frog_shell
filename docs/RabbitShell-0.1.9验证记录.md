# RabbitShell 0.1.9 验证记录

2026-11-04。Qt 6.8.3 Widgets / C++20 / MSVC，Windows x64 Release。仅 offscreen、临时配置和随机 loopback fixture；没有连接真实服务器、访问真实凭据、修改用户配置或操作可见桌面，没有实现隧道。旧 dist 保留；无 commit/reset。

## 本版实现

- 删除独立终端／文件窗口整个重复身份栏及对应 QSS。原生标题继续含连接身份和终端名／文件路径／未保存标记。终端直接开始正文，编辑保留文件路径／保存行和底部编码；路径图标与文字间留 6px。收回动作由浮窗拥有，终端追加到原右键菜单，编辑按钮复用路径行，通用浮窗也有动作菜单；不新增终端快捷键。收回先解绑动作、隐藏按钮，再移交原内容并延后销毁窗口，继续使用同一文档／PTY／组。原生关闭继续进入原 closePage 保存保护。
- 新增集中 FileIconProvider：完整主题文件名 → 最长主题后缀 → 内置语言文件名／模式／最长后缀 → 默认。比较用副本，不改真实远端路径；不读取远端内容、不发额外请求。语言扩展仅在有前导点时移除，支持 Dockerfile.* 等名称模式；含路径的通配模式同时尝试规范化路径，复杂 globstar 的完全 VSCode 等价性未验收。
- 使用本机 Seti 原始 383 个定义、238 个后缀、101 个文件名和 83 个语言映射；开发期 fonttools 4.61.0 提取 151 个原始字形轮廓。运行时 Qt QPainterPath 统一翻转 Y、等比居中，不安装字体、不加载 VSCode、不新增 Qt 模块或运行库。只导入 47 个声明 MIT 的内置扩展的 64 条语言贡献，排除单独授权扩展。输入哈希／来源见 resources/seti/source.json，导入脚本可复现。
- 浅色使用主题 light 映射，仅默认文件墨色由原浅灰调整为 #57606a 以保证白底可读。Seti 未给 ini 语言、tar.gz 等类型配置专用关联时采用默认图标，不虚构类型。zip、图片、JS／TS／TSX／JSX、Shell、README、CMakeLists、Dockerfile 和复合 spec.ts 使用原主题关联。目录使用同一矢量引擎的闭合／展开态；新建占位保留原创建语义图标。
- 资源树、普通／分屏页签、浮窗窗口图标、已有路径行共用提供器；文件及目录改名、页签迁移、主题切换原位更新。路径行在 DevicePixelRatioChange 时刷新。只按有限主题定义缓存 QIcon，目录仅两个主题引擎；不按完整路径缓存，没有新增计时器。文件树保留名称列伸展、大小列按内容收缩，避免最后一列伸展压缩文件名。
- 保留 Jesse Weed MIT 原通知及 Microsoft VS Code MIT 版权／许可，打包 NOTICE、许可证、来源摘要和主代理编写的设计文档。

## 实测与首次失败

报告目录：D:/UserFiles/Documents/ChatGPT/RabbitShell/test-results/0.1.9。下列数量含 init/cleanup。

| 套件 | passed | failed / skipped | 有效报告 |
| --- | ---: | --- | --- |
| core | 9 | 0 / 0 | core-Release.txt |
| editor_groups | 29 | 0 / 0 | editor_groups-Release.txt |
| terminal | 17 | 0 / 0 | terminal-Release.txt |
| processes | 8 | 0 / 0 | processes-Release.txt |
| controls | 6 | 0 / 0 | controls-Release.txt |
| workspace | 16 | 0 / 0 | workspace-Release.txt |
| 指定隔离 PTY／重开流程 | 5 | 0 / 0 | rabbitshell_credential_tests.txt |

首次六套件运行并非全通过，原日志保存在 ctest-initial.txt / ctest.txt：core／terminal／processes／controls 通过，editor_groups 达到 90s 超时且报告仍锁定，workspace 的 HTML 期望值失败。

超时发生在新增 dirtyCancelCloseFloatAndReturn 的原生关闭测试：生产 Close 事件延后调用 closePage，而测试 0ms 取消响应先于确认框执行，未关闭后来的模态框。将测试响应延至 10ms 后复测通过。另已将终端菜单测试断言移到 exec 返回后，无论是否找到动作都先关闭已弹出的菜单，避免断言提前返回造成菜单阻塞；本次首次超时未执行到该菜单测试，不能把两者混为同一根因。

HTML 首次测试错误地按语言预期 _html，而主题 fileExtensions.html 明确优先映射 _html_3；修正的是测试期望，生产优先级未改。随后 ctest-recheck.txt 的 editor_groups／workspace 2/2 通过。最终路径间距、列宽和菜单测试收尾后，ctest-affected-final.txt 再次 2/2 通过（6.01s）。其他四套件以上述首次运行的有效报告为证，没有把失败的旧 ctest.txt 当作六套件全通过证明，也未重跑无关集成套件。

loopbackTerminalLayoutDoesNotRestart 使用真正 fixture PTY，通过新收回 QAction 归还原组，断言没有 open-terminal／terminalClosed，不换 Session、Terminal 或 ID，收回后仍能 echo。loopbackDirectClosePreservesOtherPty 和 loopbackUnsavedReopenAndKeyCorrection 继续通过。fixture 仅回显合成字节，不执行系统 shell，不证明真实 shell 补全／SIGINT。使用 MemoryBackend 和临时密钥，不调用 nativeStoreAcrossProcess。Windows Python fixture／测试子进程指定 CREATE_NO_WINDOW。

新增 workspace 检查 24 类代表文件，覆盖完整名／复合后缀／语言／点文件／无扩展名／非 ASCII／未知类型，以及各图标 1×／1.5×／2× 输出尺寸、DPR、有效轮廓和透明角；目录 On／Off 绘制不同。通过生产 completed(read/rename) 路径验证文件和父目录重命名后的编辑路径／页签／浮窗图标，另经生产目录响应检查树图标映射；同一文档／撤销与草稿保留；已有浮窗切换两主题后图标更新。新增编辑测试检查脏文件原生关闭取消、实际路径行收回按钮和窗口 DeferredDelete；终端测试经真实菜单动作收回，确认动作随浮窗释放且不残留到内嵌终端。

## 两主题实际截图

- seti-dark-mixed-tree.png / seti-light-mixed-tree.png：混合树包含 JS、TS、spec.ts、TSX、JSX、README、CMakeLists、tsconfig、Dockerfile.*、.bashrc、JSON、YAML、INI、图片、zip、未知及无扩展名；同时显示多编辑组与路径行。
- seti-dark-editor-window.png / seti-light-editor-window.png：独立编辑客户区直接是路径／保存／收回行及正文，无重复身份栏；原生窗口标题／图标另由行为断言验证，QWidget::grab 不包括系统非客户区。
- seti-dark-terminal-window.png / seti-light-terminal-window.png：独立终端正文直接从客户区顶部开始，无空栏。

图来自真实生产 QWidget::grab 和离线合成内容。已查看两主题混合树及独立编辑／终端客户区；并由主代理人工核对 Seti 形状与删栏。未操作可见桌面，未验真实系统标题栏合成、混合 DPI 跨屏、macOS/Linux 客户端；DPI 事件处理和 1×／1.5×／2× 绘制检查不等于真实跨屏验收。未重新进行内存压力审计；本轮没有注册应用图标字体，也没有新增后台采样。

## 复现与改动文件

```powershell
$env:PATH="$PWD/.tools/Qt/6.8.3/msvc2022_64/bin;$PWD/.tools/Qt/Tools/OpenSSLv3/Win_x64/bin;$env:PATH"
$env:QT_QPA_PLATFORM='offscreen'
$env:RABBITSHELL_TEST_REPORT_DIR="$PWD/test-results/0.1.9"
cmake --build build --config Release --parallel 8
ctest --test-dir build -C Release -R '^(editor_groups|workspace)$' --output-on-failure
$env:PYTHONPATH="$PWD/.tools/test-python"
& 'C:/Users/JOKER/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' tests/run_integration.py build/Release/rabbitshell_credential_tests.exe loopbackTerminalLayoutDoesNotRestart loopbackDirectClosePreservesOtherPty loopbackUnsavedReopenAndKeyCorrection
./scripts/package-windows.ps1 -Destination ./dist/RabbitShell-0.1.9
```

实现改动：CMakeLists.txt；src/ui/fileicons.{h,cpp}；src/ui/editor.{h,cpp}；src/ui/editorarea.cpp；src/ui/workspace.cpp；src/ui/terminal.cpp；src/ui/theme.cpp；resources/seti/{vs-seti-icon-theme.json,outlines.json,languages.json,source.json,theme-package.json}；scripts/import-seti-icons.py；scripts/package-windows.ps1；scripts/verify-package.py；tests/{test_workspace.cpp,test_editorarea.cpp,test_credentials.cpp,run_integration.py}；README.md；third_party/NOTICE.md 和 licenses/{SETI-NOTICES.txt,VSCODE-LICENSE.txt}；本验证文档。主代理另维护浮窗与文件图标设计和交互检查清单。

## 交付包实测

新包：dist/RabbitShell-0.1.9（56 个文件）。EXE 为 1,451,520 bytes，SHA-256：`1B3FEF2267454E7CDA572908D840EB44DC6ACFE8E09062B0D23BBA1CE2521721`。包内有本验证记录、主代理设计、交互清单、来源摘要、Seti 和 Microsoft 两份许可证。资源随 EXE 内嵌，没有额外字体、QtSvg、WebEngine 或 VSCode 依赖。

scripts/verify-package.py 使用临时 CWD／数据目录，将 PATH 限定为包目录及 Windows 系统目录，QT_PLUGIN_PATH 指向包目录，指定 offscreen 和 CREATE_NO_WINDOW。`--version` 输出 RabbitShell 0.1.9（退出码 0）；连接管理、--demo 分别运行 1500ms，未提前退出且 stderr 为空，随后由测试程序终止。该烟测不代替正常退出保护检查。

临时图标探针加载同一生产 provider，使用包内 Qt／插件并额外加载只属于 QTest 探针的 Qt6Test.dll，执行 setiMappingAndRendering：3 passed／0 failed／0 skipped。它不读取 VSCode 资源。首次隔离探针未产生报告，是因为最小 PATH 下没有为测试程序提供 Qt6Test；修正验证脚本后通过，Qt6Test 未加入交付包，产品两种启动模式本身此前已成功。

包验证见 package-verification.json、package-icon-probe.txt；完整文件大小／哈希见 package-manifest.json，摘要见 delivery-report.md。旧 0.1.8 EXE 打包前后 SHA-256 均为 `724567061FEDB853B7C7795064D130F6A7D670B3F43B1C40DD62125C4B2C7325`，旧包内容未改。

```powershell
& 'C:/Users/JOKER/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' scripts/verify-package.py dist/RabbitShell-0.1.9 test-results/0.1.9 --icon-probe build/Release/rabbitshell_workspace_tests.exe --test-runtime .tools/Qt/6.8.3/msvc2022_64/bin/Qt6Test.dll
```

