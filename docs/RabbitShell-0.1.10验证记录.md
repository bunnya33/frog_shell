# RabbitShell 0.1.10 验证记录

2026-11-04。Windows x64 Release，Qt 6.8.3 Widgets / C++20 / MSVC。本轮分别交付 Qt 最大化按钮修正和端口面板交互设计预览。端口转发仍处于设计阶段，Qt 产品没有隧道实现或假端口入口。

仅使用 offscreen、隔离临时配置与 headless Chrome。没有连接真实服务器、读取凭据、修改用户配置／VSCode 配置或远端环境，没有操作可见桌面。后台验证进程隐藏；旧 dist 保留，无 commit/reset。

## Qt 实现与验证

- `src/ui/workspace.cpp`：沿用 checkable／checked 和唯一状态 `terminalMaximized`；最大化／还原都保留 `Icon::PanelMaximize`。tooltip／accessibleName 根据实际状态切换“最大化面板”／“还原面板”。
- `src/ui/icons.cpp`：仅对 PanelMaximize 排除通用 QIcon::On 强调色，形状、笔画与普通态图标颜色一致；选中态只由原按钮背景／边框体现。其他图标配色不变。
- 没有改变布局、内容对象、浮窗生命周期和最大化前高度的保存／恢复逻辑；没有新增样式镜像测试或 Qt 运行依赖。CMake 版本升为 0.1.10。

已有 workspace 回归通过：CTest 1/1（3.11 秒）；QTest 16 passed、0 failed、0 skipped（含 init/cleanup）。证据在 `test-results/0.1.10/ctest-workspace.txt`、`workspace-Release.txt`，构建日志为 `build.txt`。按本轮改动范围没有重复其他集成套件或内存压力测试。

生产 QWidget::grab 离屏截图：`workspace-dark-maximized.png`、`workspace-dark-restored.png`、`workspace-light-maximized.png`、`workspace-light-restored.png`。主代理已审查四张截图，确认图形与颜色相同，仅选中背景／框线切换。没有验收真实系统桌面合成、跨屏 DPI 或 macOS/Linux。

## 端口设计预览

源片段：`C:/Users/JOKER/.codex/visualizations/2026/11/02/01a19a9e-820c-7312-b18a-74e099d3e831/rabbitshell-ports-panel.html`。它是独立交互预览，不嵌入 Qt 产品，不是已实现的 SSH 功能。

紧凑工作区局部显示“终端／端口”两页签。端口表只有本地端口、远端端口、启用三列；空态或列表末尾只有一个添加入口。初始两条关闭规则。默认回环地址，非默认地址在端口单元格次行显示；高级设置收纳监听／目标地址和自启动。启用、停止、重启均为本地计时模拟，没有打开转发 socket 或 SSH 通道。

草稿先填远端时本地默认同号，手动本地值不覆盖；整数范围 1–65535。Enter／合法行外点击只保存关闭，Tab 在行内切字段，全空取消、非法保留报错，Esc 取消。用户 toggle 才显式启用；处理中禁用开关，模拟占用失败回关且不改端口号。运行中编辑必须“应用并重启”，离开行不应用；删除运行规则确认一次，取消保留。复制仅本地地址，剪贴板不可用时显示可选中文本；不打开浏览器。切页、隐藏和最大化保留规则与终端内容，隐藏有明确显示入口；最大化始终同一图标和颜色。

外围说明使用宿主前景色，产品内部使用 Dracula／GitHub Light 配色。主题选项绑定可选 host Tweak；图标使用 host Lucide。片段无 html/body、外部脚本或网络 API，CSS 限定根容器，已读回检查实际转义与脚本运行。

### Headless 实测

`tests/check_ports_prototype.cjs` 使用现有 bundled Playwright、本机 Chrome headless、新隔离 profile，skill render.py 生成的原始 sandbox iframe。未增加 allow-forms 或放宽 CSP；包装器 Lucide 请求由本地 bundled 资源响应，其他网络请求禁止。最终截图通过受支持的 widgetState 事件恢复两种外观，不依赖测试 Tweak stub；实际宿主设计控制面板未自动化操作。

最终 `ports-headless.txt` 全部通过；`ports-prototype-report.json` 记录 14 组通过、0 失败、0 runtime／console error、0 非预期外部请求：

1. 三列、两条关闭规则、唯一添加入口。
2. Dracula 产品置于浅色宿主时，外部说明使用宿主前景色。
3. 远端默认／手动本地保持、行内 Tab、Enter 保存关闭。
4. 空草稿行外取消、有效草稿行外保存关闭。
5. 0／小数／字母／溢出／负数拒绝；半填草稿、页签暂存和 Esc。
6. 处理期开关禁用、启用地址、占用失败回关且不换号。
7. A 运行草稿保留；打开 B 高级设置并重启 B 后 A 草稿不丢；明确应用仅重启 A。
8. 运行删除确认与取消保持。
9. 新草稿提交后菜单重新定位；非默认地址、IPv6、域名可见；自启动设置本身不启动。
10. 非网页本地地址复制与无剪贴板回退。
11. 最大化同形同色、切页／隐藏／重新显示不丢规则或终端。
12. 删除至空态、唯一添加入口、草稿 toggle 明确保存启用。
13. 两主题和 320px 主表／错误草稿／高级设置布局。
14. 脚本无运行错误，预览没有发出外部网络请求。

`ports-dialog-observations.json` 另记录保存前后 dialog.open、字段和错误文字。B 保存后立即检查弹窗关闭和 B 开关禁用，再等待模拟完成并检查目标确实变为 `10.0.0.12:5432`，避免仅等待 not-disabled 就误判保存成功。高级设置 Enter 经同一函数完成保存。A 草稿值仍为 8181，取消后原运行端口仍为 8080。

截图：`ports-dark.png`、`ports-light.png`、`ports-320.png`、`ports-320-validation.png`、`ports-320-advanced.png`。窄屏 iframe 实际宽度为 320px，根容器和 document 均无水平溢出；主代理已人工审查两主题、窄屏错误与高级设置截图。复制成功分支使用隔离 clipboard mock，回退分支真实走无 Clipboard API 路径；没有改系统剪贴板。检查验证设计交互，不证明真实端口占用、转发吞吐或远端连接能力。

### 失败与针对修正

- 首次宿主说明色断言读取了旧包装文件：render.py 未指定 `--force`，预览不是最新片段；强制重新生成后通过。
- 高级设置保存后 dialog 遮挡 A 的取消：原按钮 type=submit，在 sandbox 的 allow-scripts／form-action none 下被浏览器拒绝。保存前后 dialog.open 均为 true、行内错误为空，控制台明确记录未授予 allow-forms；诊断见 `ports-dialog-before-fix.json`、`ports-headless-dialog-diagnosis.txt`。修正为原生 type=button 和显式共享本地保存函数，输入 Enter 调用同一函数，移除表单提交依赖；没有放宽 sandbox。地址校验并非这次阻塞原因。
- 空态 QA 选择器 `[data-empty]` 同时匹配根状态属性和空态元素，触发 strict-mode 错误；改为 `.rs-empty[data-empty]`，生产空态无需变化。失败日志保留为 `ports-headless-selector-failure.txt`。
- 为清理截图中的先前公告尝试刷新包装页时，测试专用 Tweak stub 没有成功绑定，截图阶段失败；撤回刷新后也出现替身未注册。最终取消这个不稳定的测试依赖，通过已有 widgetState 事件恢复规则与两种外观。失败记录保留为 `ports-headless-preview-reload-failure.txt`，未因此改动产品片段或 sandbox。
- 最终完整运行以 `ports-headless.txt` 和 `ports-prototype-report.json` 为准，旧失败文件保留作历史证据，不视为当前通过日志。

## 复现与变更文件

```powershell
$env:PATH="$PWD/.tools/Qt/6.8.3/msvc2022_64/bin;$PWD/.tools/Qt/Tools/OpenSSLv3/Win_x64/bin;$env:PATH"
$env:QT_QPA_PLATFORM='offscreen'
$env:RABBITSHELL_TEST_REPORT_DIR="$PWD/test-results/0.1.10"
cmake --build build --config Release --parallel 8
ctest --test-dir build -C Release -R '^workspace$' --output-on-failure
& 'C:/Users/JOKER/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' 'C:/Users/JOKER/.codex/plugins/cache/openai-bundled/visualize/1.0.74/skills/visualize/scripts/render.py' 'C:/Users/JOKER/.codex/visualizations/2026/11/02/01a19a9e-820c-7312-b18a-74e099d3e831/rabbitshell-ports-panel.html' "$PWD/test-results/0.1.10/ports-preview.html" --force
& 'C:/Program Files/nodejs/node.exe' tests/check_ports_prototype.cjs
./scripts/package-windows.ps1 -Destination ./dist/RabbitShell-0.1.10
& 'C:/Users/JOKER/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' scripts/verify-package.py dist/RabbitShell-0.1.10 test-results/0.1.10
```

实现／交付修改：`CMakeLists.txt`、`src/ui/workspace.cpp`、`src/ui/icons.cpp`、`tests/check_ports_prototype.cjs`、上述片段、`scripts/package-windows.ps1`、`scripts/verify-package.py`、`README.md`、本记录及 test-results/0.1.10 证据。包验证脚本按实际版本检查验证文档，图标探针改为可选，本轮只跑相关启动烟测。主代理维护隧道方案、交互清单和通用 Qt 规范；更新后的三份文档随新包提供。

## 新包验证

独立目标 `dist/RabbitShell-0.1.10`，58 个文件，EXE 1,451,008 bytes。包内与构建 EXE SHA-256 相同：`3693E48867B05B940E40FFEFE8C2F99C5DD1504AD59A4577C0C900641E017E5C`。

`scripts/verify-package.py` 将 PATH 限定为包目录与 Windows 系统目录，QT_PLUGIN_PATH 指向包目录；使用临时 CWD／数据目录、offscreen 和 CREATE_NO_WINDOW。`--version` 输出 `RabbitShell 0.1.10`，退出码 0；连接管理与 `--demo` 两种模式分别持续运行 1500ms，未提前退出，stderr 均为空，然后由烟测程序终止。此检查验证部署启动，不替代正常退出保护验证。本轮没有重复 0.1.9 已验证的包图标探针。

包内同步最新 README、本验证记录、主代理隧道方案、交互清单、Qt 通用规范及既有 Seti／Microsoft 许可证。端口设计片段不作为 Qt 运行资源打包；没有增加 WebEngine、浏览器或脚本运行库。

旧 0.1.9 EXE SHA-256 保持 `1B3FEF2267454E7CDA572908D840EB44DC6ACFE8E09062B0D23BBA1CE2521721`，旧 0.1.8 保持 `724567061FEDB853B7C7795064D130F6A7D670B3F43B1C40DD62125C4B2C7325`，两包均未修改。

证据：`package.txt`、`package-check.txt`、`package-verification.json`。最终文档同步后重新生成 `package-manifest.json`，记录全部 58 个文件的大小／SHA-256，交付摘要为 `delivery-report.md`；均位于 `test-results/0.1.10`。没有把失败或过期报告替代最终通过记录。
