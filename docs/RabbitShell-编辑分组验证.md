# RabbitShell 0.1.3 编辑分组验证

0.1.8 更新：终端进入编辑区的旧测试已改为拒绝迁移，并验证独立浮窗返回原组、原 PTY 与编辑布局不变。下文移到编辑区的描述是旧版验证历史；当前证据见 [0.1.8 验证记录](RabbitShell-0.1.8验证记录.md)。

后续 0.1.6 已验证同行工具、所属组路由、工具区拒绝拖放、菜单存活保护、左右关闭按钮回收及惰性查找，editor_groups 为 29 passed；详见 [0.1.6 验证与内存记录](RabbitShell-0.1.6验证与内存记录.md)。以下保留 0.1.3 历史证据。

2026-11-03。Windows 11 x64 / Qt 6.8.3 / MSVC Release；使用独立临时配置、合成文件与 loopback fixture，未读取或连接用户服务器。

## 自动测试

| 测试 | passed | failed | skipped | 证据 |
| --- | ---: | ---: | ---: | --- |
| core | 9 | 0 | 0 | `test-results/0.1.3/core.txt` |
| editor_groups | 23 | 0 | 0 | `test-results/0.1.3/editor-groups.txt` |
| SSH/SFTP fixture | 3 | 0 | 0 | `test-results/0.1.3/rabbitshell_session_tests.txt` |
| credentials + live PTY layout fixture | 14 | 0 | 0 | `test-results/0.1.3/rabbitshell_credential_tests.txt` |

常规 `ctest --test-dir build -C Release --output-on-failure` 为 3 passed / 1 skipped（ssh_sftp 没有端口）；上述两个 fixture 报告来自 `tests/run_integration.py` 启动的真实本机随机端口，均没有 skip。核心凭据实现和 Editor::reload 的 payload 生命周期没有回退。

分组覆盖左右任意空组、连续同排三／四列、移空中间列、四列窄窗不换行、保持其他列尺寸、半列预览与最终落点、全部合并、显式上下嵌套、关闭最后页、最后页浮窗／收回、dirty 取消、批量关闭嵌套事件、页面／文档／撤销／滚动身份、进程页唯一性与终端输出保留。

拖放用例直接发送 Qt dragEnter / dragMove / drop 到实际页签组与子 viewport，覆盖同组排序、跨组中心、四边、假 MIME、跨区、较新区域隐藏时的合法旧区拖放、leave／Esc／来源失效／目标清理。offscreen 的 QDrag::exec 仅做可用平台烟测；它立即返回且 cancel 未实现。关闭和取消的嵌套循环使用受控的 executeDrag 边界回归，不能据此声称已完成 Windows OLE 鼠标拖放验收。

活 PTY fixture 在移到编辑区、拆分、浮窗、收回及返回终端面板后，验证同 Session／Term ID／Terminal 控件、终端仍运行、原通道 echo 成功、无额外 open-terminal、无 terminalClosed、初始 prompt 仅一份。

## 实际渲染

`test-results/0.1.3` 当前验收图：

- `dark-3-columns.png` / `light-3-columns.png`：三列单排，完整高度。
- `dark-4-columns-status.png` / `light-4-columns-status.png`：四列单排、状态卡片及正确活动栏选中标记。
- `dark-drop-preview.png` / `light-drop-preview.png`：四列布局中的目标半幅预览。
- `dark-narrow.png` / `light-narrow.png`：1024×760 仍保持四列同排，终端工具栏不超过 48px。
- `dark-icon-tooltip.png` / `light-icon-tooltip.png`：实际 QToolTip 控件独立渲染；`dark-drag-tab.png` 为 QDrag 使用的真实拖影片。
- `dark-transfer-frame.png` / `light-transfer-frame.png`、`dark-connection-manager.png` / `light-connection-manager.png`：传输外框与连接管理分区。

旧 `*-3-groups.png` / `*-4-groups-status.png` 是需求澄清前的草稿，保留但不作为本轮验收图。

测试 setup 显式加载系统 Microsoft YaHei 与 Consolas，检查中文“编”有字形；所有 demo Editor 登记到工作区并刷新语法，断言浅／暗主题脚本注释和字符串实际格式色。截图为 QWidget::grab，DPR 与 QPA 记于 render-context.txt。主代理已另用正式桌面工具核对拖影、预览、Esc 和放置结果，范围见下文；实际混合 DPI、窗口外释放和工具单手势释放时序仍未完整覆盖，离屏图不替代该范围。

## 交付与桌面审查

主代理负责设计与只读验收。已生成独立 `dist/RabbitShell-0.1.3-preview`，用 `--demo --data-dir <独立目录>` 提供桌面审查。审查记录完成后生成正式 `dist/RabbitShell-0.1.3`，二者 exe 相同，正式包带最终文档与主代理设计。旧 0.1.2 目录及用户窗口保留；未提交 Git。

## 原生桌面审查结果

主代理通过正式 Computer Use 工具选择本轮 `0.1.3-preview` 可见 Normal 窗口（PID 13856、独立 `build/native-review-0.1.3-20261103-153115`），预览 exe SHA-256 为 `4286105426D2831B49D74802C769D760917DD5853C58739AB2DE376254D22BC2`。先前 Hidden 启动的 PID 46904 没有可交互主窗口，已仅停止该自建预览进程；用户旧实例未操作。

主代理实际观察到原生小页签拖影、中心和右侧半幅预览；Esc 清除预览并保留源页。将中列 deploy.sh 放到 config 正文中心后，三列自动变成两列，原内容保留。

正式工具 `sky.drag` 后拖放仍处于挂起状态，再观察也未结束；在同一落点追加 `sky.click` 释放后才提交。当前只能记录为合成输入与 OLE 释放时序的待定范围，不能声称单次拖放手势完整通过；没有据此修改正常 Qt 拖放或消息观察钩子。后续原生操作已验证从两列拖右边缘生成三列，再沿右边缘生成四列，均为同排、完整高度；其他列宽保留，结果与半幅预览一致。点击全部合并后四页在单组，无残留空分屏，正文与两终端原输出保留。

退出确认阶段用户按Esc停止桌面操作，后续未再控制窗口，是否退出未再核验。最后四文件／两终端在合并完成时的一次快照为 Private 81.82 MiB、Working Set 128.26 MiB，仅为该场景单次样本，不是上限或长期基线。

静态复核：生产路径仍为 QDrag::exec、QMimeData 和 Qt drop 事件；WH_GETMESSAGE 仅观察 Esc／左键释放并无条件 CallNextHookEx，不吞消息、不注入释放。当前没有证据据此重写正常 Qt 拖放。可确认原生拖影／overlay／Esc／Drop 结果，不能标记常规鼠标单手势或工具单手势释放已全面通过。真实混合 DPI、窗口外松手浮窗及其他平台仍保留相应验收边界。

正式 exe SHA-256：`4286105426D2831B49D74802C769D760917DD5853C58739AB2DE376254D22BC2`。打包时逐项确认 Qt／OpenSSL／MSVC runtime、设计及验证文档，生成 `test-results/0.1.3/package-manifest.json`；正式 exe 与 Release／preview 一致。无本轮新增功能范围。
