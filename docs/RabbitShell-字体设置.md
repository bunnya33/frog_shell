# 字体、编辑器行距与界面缩放（0.1.29）

设置保留共享等宽字体、字重及编辑器 / 终端 / 服务端口列表三类基础字号。内置 Cascadia Code NF，默认 Medium 500，三类默认 14 逻辑 px，可分别设置 8–40 px。已有 15 px 编辑器、14 px 终端等偏好保持原值。UI 工具栏、菜单、搜索和 Find 使用系统字体。

“界面缩放”独立于基础字号：默认 100%，按 `1.2^level` 变化，level 为 −4…4，约 48%…207%。Ctrl++（也支持 Ctrl+=、Ctrl+Shift+=、数字键盘加）、Ctrl+−、Ctrl+0 在应用窗口及菜单内调整或恢复。macOS 使用物理 Control（Qt MetaModifier）；Windows/Linux 使用 ControlModifier。普通加减及其它修饰组合保持输入。Ctrl+↑/↓ 恢复编辑 / PTY 操作；Ctrl+滚轮不再改变内容字号，并阻止 Qt 编辑器内置局部 point zoom。

字号设置显示基础逻辑 px；内容字体输出为基础值乘界面倍率，按逻辑像素取整。缩放不修改 family / weight / editorPx / terminalPx / listPx，也不重建文档、终端或列表。已打开、隐藏、浮窗及之后创建的组件即时应用。

实现位于 `src/ui/uizoom.*`：公共 QProxyStyle 尺寸度量、统一 canonical QSS，以及带 QObject 生命周期的显式基准布局 / 图标绑定；自绘坐标与命中使用同一换算。代理递归读取原始 metric 后只乘一次，StyledCombo 使用同一代理策略。每次从基础值计算，不捕获布局算出的实际 geometry 再放大。视图列宽以基础单位记忆。定时器、端口、行数和字节上限不缩放。

配置为 QSettings `appearance/uiZoomLevel` 和 `appearance/font/{family,weight,editorPx,terminalPx,listPx}`。先完成 data-dir / 测试配置再惰性加载；绘制时不读配置，滚轮不强制写盘。字体只注册一次，失效 family 回退内置，保留中文系统回退。

0.1.29 设置中的字号、界面缩放等数值调整控件需先点击启用且保持焦点，滚轮才修改数值。未启用时滚轮传给设置页面的父滚动区，点击标签、空白或其他区域退出滚轮编辑；键盘调整及已展开下拉列表的滚轮选择保留。此规则只作用于设置，不改变编辑器或终端的滚轮行为。

编辑器行距采用最终参考图的比例：默认 Cascadia Code NF、Medium 500、14 逻辑 px 字号下目标行高为 19 逻辑 px；其他字号及 UI zoom 后按实际字体像素乘 `19/14` 取整，并保留更高的自然字形高度及末块 document margin。不是固定增加 2px/4px，不修改用户基础字号，也不改变终端行距。行号按实际文本基线绘制，中英文回退字体、组合字符和空行使用一致几何。

仍使用 QPlainTextEdit/QPlainTextDocumentLayout 的惰性布局；命中、光标、选区、翻页、滚动及查找定位读取同一 block 几何，不通过插入空行或文本格式改变文档。布局未新增每 block 缓存，文档按 Qt 所有权随编辑器销毁；此说明不代表全面内存泄漏排查。草稿、文档对象、修改状态和原撤销/重做栈在字号、主题及倍率变化时保持。真实实装截图和度量见 [0.1.29 验证记录](RabbitShell-0.1.29验证记录.md)；早期 +10%/+20% PNG 仅为已取代的视觉提案。

应用倍率与系统 DPI 独立；不修改 Qt 私有高 DPI 状态、不重启、不做整窗位图缩放。窗口外框和系统文件对话框遵从 OS。内容溢出通过滚动 / 省略显示；外框不随倍率主动重建或 resize。字体度量的整数取整会产生少量像素差异。

终端仍使用 [0.1.16 重排修复](RabbitShell-0.1.16终端重排修复方案.md)：主屏宽字 / 组合字符及有界历史保留；历史放大不合并已有物理行；alternate screen 保留固定网格裁切语义。本轮不更改引擎补丁。验证见 [0.1.17 记录](RabbitShell-0.1.17验证记录.md)。

0.1.18 的“重置所有设置”与“恢复字体默认”独立：确认后移除 `appearance/process-table/services/network/files/downloads/window` 偏好组，依次 reload 字体、倍率、Dracula，并同步已有列表缓存、停止列宽保存 timer、恢复本地网卡及隐藏文件视图。不调用 QSettings.clear，不访问凭据后端或远端；普通新操作仍可再次保存偏好。窗口恢复正常1280×850并用既有屏幕适配限制尺寸，正常关闭保存新几何。实际验证见 [0.1.18记录](RabbitShell-0.1.18验证记录.md)。
