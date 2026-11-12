# FrogShell 0.1.41 验证记录

2026-11-11，Windows 11 x64 / Qt 6.8.3 / MSVC Release。仅使用临时配置、offscreen 控件和私有 loopback；没有连接真实服务器、读取真实凭据或操作系统剪贴板。

## 实施和定向验证

| 范围 | 本轮生产证据（test-results/0.1.41/ 下） | 结果 |
| --- | --- | --- |
| 端口 UI、保存意图与恢复 | worker-ports-integrated/ports-ui-tests.txt | 18/0 |
| 隧道并发、占用与实际转发 | worker-tunnel-second/tunnel-tests.txt；worker-tunnel-integrated/tunnel-tests.txt | 23/0；新增定向 6/0 |
| 数据迁移 | worker-storage-test-output.txt | 5/0 |
| 工作区身份撤销、目录激活与下载来源 | worker-workspace-integrated/tests.txt | 10/0 |
| 时钟面板、确认期间锁定 | worker-clock-integrated/tests.txt | 7/0 |
| 真实 Workspace 时钟与主 PTY 并存 | worker-clock-workspace/workspace-clock-tests.txt | 3/0 |
| 语法诊断 | worker-syntax-integrated/tests.txt | 35/0 |
| 清理安全边界与真实确认 | worker-cleanup-integrated/tests.txt | 10/0 |
| 日志 | worker-diagnostics-integrated/tests.txt | 6/0 |
| 编辑限制、日期/数字/网卡滚轮及设置 | worker-controls-integrated/tests-popup-final.txt | 10/0，零跳过 |
| 大文件读取、读取中增长、降低限制后保存、旧 hash 冲突 | controls-session-mini/results.txt | 3/0，真实 loopback fixture |

NIC 专项原始失败保留：下拉框弹层在独立窗口中，`QWidget::isAncestorOf` 不跨该边界；弹层关闭还会产生窗口激活往返。最终 guard 用 QObject 所属关系保留自身弹层资格，在应用失活、焦点转至其它控件、程序/Tab 聚焦或点击外部时清除。新报告是在去掉临时 trace 后重建运行，没有将旧失败改写成通过。

主线程独立验收：端口 9/0、存储 8/0，checkpoint3 语法 21/0、滚轮 5/0、时钟 5/0、真实设置页资源清理 7/0。guard 最终改动提供匹配 checkpoint4，主线程新增 activation 用例连同滚轮回归 6/0 通过；旧 checkpoint3 的 5/1 红项保留。其 main-* 证据只读。

## 可视证据

端口占用提示：`worker-ports-integrated/port-occupied-dark.png`、`port-occupied-light.png`。时钟：`worker-clock-integrated/worker-clock-dialog-dark.png`、`worker-clock-dialog-light.png` 和 `worker-clock-workspace/clock-workspace-dark.png`、`clock-workspace-light.png`。设置/空间统计暗浅截图在 `worker-controls-integrated/`。语法诊断及真实清理确认/完成反馈已由主线程独立截图审阅。

## 冻结与交付

本版本仅新建 `dist/FrogShell-0.1.41/` 与 `dist/FrogShell-0.1.41-portable.exe`。源码清单/ZIP、目录 inventory、逐文件 payload 校验、双 PE 原生图标及隔离启动结果统一记录在 `test-results/0.1.41/release-report.json`。旧 dist、0.1.40 证据和批准参考按主线程基线逐 hash 保留；main-* 不修改。

验证边界：下载反馈症状未复现，仅增加来源诊断与零下载导航回归；语法诊断不是 schema 校验；空间统计是有界逻辑大小；安全清理当前仅支持 Windows；真实服务器时钟权限与各平台桌面合成未实机验证。启动 smoke 只终止自身测试进程，不代表再次验收全部退出流程。关于/更新和树形终端原型未接入本版本。
