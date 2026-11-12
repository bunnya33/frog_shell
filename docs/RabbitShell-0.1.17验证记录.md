# RabbitShell 0.1.17 验证记录

本版将内容局部缩放改为全应用界面缩放。公共 Qt Widgets API、canonical 尺寸绑定与独立基础字号的实现说明见 [字体与界面缩放](RabbitShell-字体设置.md)。保留 0.1.16 的 libvterm 窄补丁与原始字体，不更改引擎或远端流程。

## 已完成的隔离验证

Release 构建成功。Windows / Qt 6.8.3，临时 QSettings 与 data-dir、QT_QPA_PLATFORM=offscreen、合成数据；未连接真实服务器，未使用真实凭据 / known_hosts，未操作可见桌面。

9 个受影响 CTest 目标全部通过，QTest 合计 **134 passed / 0 failed / 8 skipped**（包含各目标 init / cleanup）：

| 目标 | passed | failed | skipped |
| --- | ---: | ---: | ---: |
| appearance | 28 | 0 | 0 |
| terminal | 18 | 0 | 0 |
| editor_groups | 29 | 0 | 0 |
| core | 9 | 0 | 0 |
| workspace | 16 | 0 | 0 |
| ports | 9 | 0 | 2 |
| services | 11 | 0 | 6 |
| controls | 6 | 0 | 0 |
| processes | 8 | 0 | 0 |

8 项 skip 均缺少本机 SSH loopback fixture（端口 2、服务 6），没有计为通过。未做真实 systemd / SSH / 多显示器验收，不重复旧 launcher 或内存压力矩阵。

重点验证：编辑基础15px / 终端14px保持，实际显示字体随倍率变化；图标、布局、侧栏、页脚、菜单/下拉度量同比缩放；底层样式 identity 与往返无漂移，外部 style 销毁后可重新安装；普通加减与无关修饰不拦截，缩放不输出 PTY 字节，Ctrl上下正常输出；Ctrl滚轮不触发 Qt 编辑器局部 zoom。覆盖设置比例/加减/重置、字体重置独立、持久化重建、主题切换、新建/隐藏/浮窗、文档与选区保留；列宽按基础单位保存；端口草稿不提交/重启/重置，服务不发查询。

独立 DPR=1.5 定向运行 **7 passed / 0 failed / 0 skipped**，确认系统 DPR 与应用倍率 / 基础字号分离。这是 offscreen 事件及渲染证据，不等于真实 150% 桌面或跨显示器验收。主屏重排 / 宽字 / 光标 / 选区 / alternate 的既有受影响回归一并通过，历史放大不合并旧物理行、alt 固定网格等边界不变。

双主题 100% / 120% / 144% 工作区及连接管理截图经开发与主审实际查看：`test-results/0.1.17/zoom-{workspace,manager}-{dark,light}-{100,120,144}.png`。设置截图与 DPR1.5 截图在同目录及 `dpr150/`；offscreen 插件不实现原生外框、真实遮挡或硬件动画。未重测整体资源压力；实现使用共享状态、显式生命周期绑定与有界既有终端历史，不以算法设计替代实测内存结果。

## 交付冻结与最终证据

独立目录 `dist/RabbitShell-0.1.17`、单文件 `dist/RabbitShell-0.1.17-portable.exe`。文档先冻结进目录，再 pin inventory，显式 `-Version 0.1.17 -Source ... -Manifest ...` 打包；兼容脚本默认0.1.12不变。未改变 launcher 核心。

目录最小 PATH 启动、portable 静态与 version / manager / demo 三项隔离烟测在打包后执行；**本文件冻结时不提前声明通过**。完成状态、真实哈希 / size / payload / 内部 EXE、源文件清单与旧10目标340文件复核，以 `test-results/0.1.17/final-report.json`、`source-files.json` 及各实际报告为准。烟测使用临时私有配置、中文含空格目录仅一个 EXE，GUI 子进程由测试结束，不代表优雅关闭或真实服务器验收。
