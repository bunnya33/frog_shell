# RabbitShell systemd 服务实现记录

2026-11-05。已接入 Qt Widgets。0.1.14 追加模板与局部查询兼容修复，原 0.1.13 写删/连接实现边界不变。此记录描述实现与限制；真实 systemd、真实服务器及跨平台客户端尚未验收。

0.1.14 区分模板 `name@.service` 与实际实例 `name@instance.service`。合法模板仍在列表显示 list-unit-files 返回的真实自启状态，不给模板编造 ActiveState/SubState，也不发送 systemctl show；状态为“模板（需指定实例）”。模板行可查看列表已知详情/复制名称，其余运行、自启修改、日志和删除操作禁用并解释限制；后端独立拒绝模板控制，新增表单拒绝创建共享模板，不自动选择 DefaultInstance。实例查询和控制照常，实例删除仍遵守既有保守限制。

列表批次即使 exit 非零，也仅采纳请求范围内具有 Id/LoadState/ActiveState/SubState 的有效具体服务属性块；not-found、不完整或后续未读取的 unit 标为“未知 / 读取失败”，保留已知文件自启状态并显示原始错误。systemctl 在首个失败项后停止输出，未读项不误标 inactive。没有新增逐行回退/自动重试，批量上限仍为 96、列表上限 5000。所有具体 unit 均无可用属性时整次列表报错；实际 manager 探测/枚举失败或 transport unknown 仍失败，不输出伪成功列表。此容错只用于只读列表，变更步骤仍失败即停。严格模拟 fixture 及修复前失败证据见 [0.1.14 验证记录](RabbitShell-0.1.14验证记录.md)。

底部第三页为“服务”，没有侧栏入口；同行加号/刷新属于服务页，最大化/还原/隐藏共用。首次显示才创建 command-only SSH Session。每工作区一个查询/控制 worker、一个 5 秒计时器；隐藏、切页、切服务器停止周期刷新，在途控制不会因此撤销。退出、主动断开、关闭工作区只结束本地连接，不调用远端 stop。独立连接不依赖 SFTP 成功；认证、known_hosts（含 CRLF）、密码保存复用现有流程。服务连接和文件选择连接均纳入退出等待；身份、凭据代次或仓库 revision 失效阻止后续请求。

列表合并 list-units 与 list-unit-files，再以每组最多 96 个 unit 读取列表必需属性；总数最多 5000，单次 stdout/stderr 合计最多 2 MiB。按完整 unit 名称增量复用 item，保留选择、排序、滚动和本地列宽。active/exited 显示“已完成 / 退出”。静态、间接、屏蔽、自启与运行状态分开。当前行复用一个操作按钮，按下时捕获 unit；不为每行创建 widget、连接或定时器。详情和日志按需，日志最多 200 条/256 KiB，无跟随；缺少 journalctl 不影响列表。表头和圆角卡片支持 Dracula / GitHub Light。

所有 libssh2 操作仍由所属 Session 的单个线程执行。服务执行采用独立非阻塞路径，覆盖打开通道、exec、stdin、EOF、stdout/stderr、close、exit code；读取 15 秒、变更 90 秒上限，超时/断线/输出超限标“结果未知”，结束当前 worker，不自动重新连接或重放。命令禁 pager、颜色和交互授权，没有隐式 sudo、终端注入或远端工具安装。控制串行化；确认框/菜单期间合并刷新，避免嵌套事件循环丢失用户操作。空闲 command-only worker 按 keepalive 期限等待，submit/stop 可唤醒。

新增包含名称、描述、绝对程序/脚本、参数、可选已有解释器、工作目录、已有运行账号、异常重启、自启及立即启动。后两项独立。只读预览由后端同一生成器产生；远端选择器只负责当前服务器的 SFTP 路径，独立维护历史和单行地址/面包屑，不改主资源树。加载中禁用确认，取消保留表单；SFTP 不可用时仍可手填。检查账号、UID/GID、普通文件/目录、Unix 属主/组权限和祖先目录搜索权限，不执行所选程序。ACL、LSM、mount 标志、二进制兼容性、脚本 shebang 和依赖不是静态检查的成功保证；实际启动由 systemd 和后续状态复查判定。

单元字段不接受换行、控制字符。User 只用已校验原值；Description / WorkingDirectory 按标量写入、将 % 写为 %%；拒绝首尾空白和末尾反斜杠，保留内部空格、中文、引号和反斜杠。ExecStart 每个 token 分别双引号并转义引号/反斜杠/%；参数中的 $ 写为 $$，禁止环境展开。首个可执行路径禁止 $（这是 systemd 自身限制）；脚本作为解释器后的数据参数可含 $。参数空格分隔，单/双引号分组，反斜杠转义下一字符；不 eval、不使用 shell 来运行程序。Type=simple 不增加 Type=exec 的 systemd 240 要求；未声明任何旧版 systemd 已实测。

创建前拒绝已加载、已安装同名、mask/alias，最终 /etc 写入使用 shell noclobber 排他创建，stdin 只传单元配置。依次写配置、daemon-reload、按需 enable、按需 start、读取状态。失败即停，列出已完成步骤和未确认步骤；写失败可能留下部分文件，不自动删除或重放。权限不足由当前 SSH 身份得到明确错误。创建/删除额外要求服务器现有 getent、id、GNU stat、cat、sha256sum、rm 等基础工具；缺少时限制该操作，不安装依赖，查询列表仍仅依赖 systemctl。

删除先按需核对完整属性、路径、来源、激活关系及文件指纹。只支持 /etc/systemd/system 下独立普通 .service，拒绝模板/实例、供应商、链接/alias/mask、drop-in、generated/transient 和未知来源；存在 TriggeredBy 时要求先处理 timer/socket，不自动删关联。当前实现进一步要求根目录链不为链接、unit root 属主且单硬链接、/etc/systemd/system 为 root:755、unit 模式为 644/640/600/444/440/400、当前 SSH 用户能写父目录。这是首版的明确保守权限边界。

确认显示服务器、unit、配置路径、运行/自启和关联，默认取消；确认后重新核对属性与文件 device/inode/mode/uid/gid/nlink/size/mtime/ctime/SHA256。活动服务 stop 后复查，然后 disable、再次复核、同一 shell 立即核对 stat/hash 后仅 rm 精确路径、daemon-reload、刷新。每一步失败都停止后续步骤；保留程序、工作目录和业务数据，不递归清理。此方式不能对抗同时拥有 root 的恶意修改者在最后检查和 unlink 之间制造竞争，也不承诺处理所有未知依赖；复杂来源明确拒绝自动删除。

主要源文件：src/core/services.{h,cpp}（序列化/策略/步骤），src/core/session.{h,cpp}（模式及有界执行），src/ui/services.{h,cpp}（列表/表单/选择器），src/ui/workspace_services.cpp（认证及生命周期），workspace/mainwindow/CMake 为少量接入。tests/test_services.cpp 和 tests/service_fixture.py 覆盖模拟策略与真实 loopback SSH 协议；fixture 从不执行 shell 或 systemctl，不能替代真实 systemd 验收。
