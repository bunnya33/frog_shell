# 圆角边框与裁切

0.1.25，2026-11-10。保留现有圆角容器和原生 Qt 控件交互，只修正边框四角缺失、子内容覆盖及缩放时裁边。

## 绘制规则

`roundedsurface` 共用外轮廓和描边几何：半径描述外轮廓，画笔中心内缩实际线宽的一半，中心线半径相应减半线宽。背景填充外轮廓，描边完全留在容器内，不使用固定 0.5 内缩。

已有 card/transfer/find、进程卡和路径框各按需创建一个鼠标穿透、不获焦的轻量描边子控件，保持在子内容上方。它只覆盖外轮廓以外的角区，并补完整边框；角区使用所在平面已定义的主题底色。只作用于已确认的平面语义容器，不给普通布局加框，不为每一行创建图层，无周期 timer、位图缓存或图形特效。父控件负责生命周期，重复安装复用同一个对象。

## 审计与接入

| 位置 | 处理 |
|---|---|
| frameRole card | 5 逻辑像素外圆角；QSS 保留透明边框占位，共享描边在最上层 |
| transfer/find | 分别保留 5/4 逻辑像素圆角，使用对应语义分隔色 |
| 服务卡 | 删除 radius 8 的整窗 mask，统一到原卡片 radius 5；表头/viewport 不再裁去外框 |
| 进程卡 | 保留 radius 8、完整表头与表体大卡片；共享半线宽内缩替换固定 0.5。表格子内容仍局部裁切，不裁外层描边、不新增内部细线 |
| 收藏卡、连接树、端口表格 | 沿用 card，最上层描边保护子 header/viewport/滚动条处四角 |
| 状态摘要、字体预览、连接 loading/bar | 沿用 card，无额外业务状态或布局包装 |
| 顶部面包屑路径框 | 5 圆角；uploadTarget 使用 2 逻辑像素强调描边，清理后恢复原细线 |
| QMenu/子菜单 | Qt 客户端 alpha 背景与 radius 6 完整描边；保留 QMenu 的 action、键盘、子菜单、关闭和可访问性语义 |
| StyledCombo 弹层 | 保留 Qt 自有 popup 和 selection model，构造时只对确认是 Qt::Popup 的容器设 alpha；半线宽内缩、同轮廓背景和描边，删除原 whole-mask |
| 普通按钮、输入框、关闭态 combo | 既有 QSS 圆角已覆盖背景和边框，继续沿用；输入框焦点及上传路径强调色保留 |
| ThemeChoice、开关、图标和拖影 | 审计既有留边与绘制区域，无父级 whole-mask 裁掉描边；未为它们追加容器边框 |
| rail/footer/section、普通布局 | 原直线分区保持，不以直角替代任何圆角，也不增加新卡片 |

菜单安装器只对 Polish/Show/WinIdChange 检查 QMenu，无每事件转换、轮询或原始文本日志。Windows 后端在实际 HWND 上尝试公共 `DWMWA_WINDOW_CORNER_PREFERENCE=DWMWCP_DONOTROUND`，防止系统另一套圆角再次裁掉客户端描边；不修改主窗/对话框原生标题栏。高对比度分支恢复系统 corner preference；未在测试中改变用户系统高对比度。

不支持该 DWM 属性时使用同外轮廓的保守 region，包含抗锯齿包络；popup Resize 和 zoom 更新该区域，后续成功则清 mask。测试用受控 fallback 状态验证区域更新和清除，当前 Windows 主机实际调用为 S_OK，不冒充旧 Windows 实机验证。

## 证据边界

暗浅主题、zoom -1/0/+1、DPR 1/1.5/2 的真实 Qt 客户端 render 和四角像素探针用于检查线条与相邻像素。DPR 是 render 仿真，不是三台显示器或真实混合 DPI 拖动验收。

另有 Qt windows 后端的隐藏 HWND 测试：所有顶层窗在 Polish 设置 WA_DontShowOnScreen；实际 DWM setter/getter 和 Qt 客户端 grab 均检查。它没有创建可见用户桌面窗口，也不是 CreateDesktop 隔离桌面或 DWM 合成桌面截图。合成阴影、真实多屏、旧 Windows 和真实高对比度外观仍未实机验收。具体报告和截图见 0.1.25 验证记录及交付报告。
