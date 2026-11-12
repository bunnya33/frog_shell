# RabbitShell 0.1.21 上传预览与本地诊断

任务日期：2026-11-09。用户报告上传后确认删除导致窗口未响应。主审提供的 Windows 事件为 Application Hang / AppHangB1，未见访问冲突证据；本轮未读取或转储用户真实会话。**隔离环境尚未复现该 Hang，根因未定位，不宣称已根治。** 本版交付明确的上传目标预览修正及用户授权的低开销诊断日志，不猜改删除算法。

## 上传目标预览

沿用 .20 的纯目标解析：目录→自身，普通文件→父目录，空白→已提交浏览根；真实本地 URL、Copy、菜单目标快照均保留。展开目录框住目录行及所有可见展开后代，收起目录仅框一行；普通文件不单独描边。根目标框住列表 viewport 并高亮顶部路径，保留底部「上传到 /绝对路径」。原选择不变，系统拖影/复制标识沿用 OS/Qt。

绘制仅遍历可见目标子树；父行在屏幕外时从第一可见行开始，hidden/filter 行不计入，不包含下一同级，裁剪到 viewport。结构、数据、展开/收起、过滤、离开、drop 和 hide 清理。有效 render 在 QSignalBlocker 前主动清除预览，避免树信号被屏蔽后路径框残留。无新 timer 或整树 hover 缓存。

## 本地日志

设置「诊断日志」显示实际路径，提供打开目录和复制路径。默认目录为 `QStandardPaths::AppLocalDataLocation/logs`，不在 portable 解压 runtime；`RABBITSHELL_DATA_DIR` / `--data-dir` 优先，测试使用私有目录。启动在参数与数据目录处理后，`--version` 不启动日志。

- 仅结构化 enum 白名单：应用版本、相对单调毫秒、UI/worker、随机 Session tag、job id、数量、截断路径的 SHA-256 前24位。无 host/user/profile、密码/密钥、信任记录、终端命令/输出、文件内容、环境、配置或原始错误字符串；不接管 qDebug/Qt warning。路径只用于前后关联，原名不落盘。
- 上传 submit/begin/done/failed/cancel；删除 confirm/accepted/cancelled/submit/worker begin/done/failed；UI 删除 begin/end、具体 remove begin/end；list request/submit/result/discard/worker结果；render begin/sort/filter/layout/end。`DeleteUiBegin` 是进入同步阶段，`RemoveEnd`/`DeleteUiEnd` 才表示对应调用已返回。ListRequest 在目标 lookup 前，ListSubmit 有实际 job id。
- 内存队列最多512条/256KiB；写线程交换 batch 后队列可再次填充，队列+在写 batch 的日志负载合计至多约512KiB，另有少量定长元数据/单条编码。溢出丢记录并汇报计数，不阻塞等待队列空间。GUI enqueue 锁仅移动有界内存；写入、轮转、flush 不持此锁。
- 独立轻量 worker 约每秒处理一次批次并 flush，不逐条 flush。当前 `diagnostic.jsonl` 加 `.1/.2.jsonl`，共3份，各最多2MiB；轮转失败停止进一步写入，不突破上限。正常约5秒记录一次 heartbeat；UI 每500ms仅更新内存心跳，后台发现停顿≥2秒记录最后UI阶段/心跳年龄，持续停顿最多每5秒一条。退出停止timer、清空active指针并排空worker队列。
- 日志尽力写入本地文件；磁盘不可写/空间不足、强制终止或系统断电可能丢失尾部记录，不是崩溃转储或根因证明。再次遇到未响应，可等数秒后通过设置所示目录提供三份 JSONL。

## 本轮验证

Release / Windows x64 / Qt6.8.3；仅 offscreen、私有配置、隐藏测试进程和 loopback SFTP。

CTest **6/6**，含 init/cleanup 合计 **91 passed / 0 failed / 5 skipped**：diagnostics 4/0、native_drag 10/2、file_transfers 14/3、appearance 31/0、core 9/0、workspace 23/0（此处为 pass/skip）。三项上传 fixture skip 在独立实际 SFTP **5/0/0** 中覆盖；两项原生拖出 SFTP fixture 本轮未运行，常规 COM/UI 回归通过，不伪称实机验收。未重跑无关 SSH/launcher 压力矩阵。

- 所选/当前目标删除：名称/大小排序、展开目录；深滚动 `/root/bin` 多子树、pending sort、根/子目录旧 list、所选子树消失；未复现 Hang。
- 实际 SFTP 上传→完成局部刷新→选中当前行→Delete 确认期间收 list→确认删除→节点移除/父 list；同名取消后成功替换再删；取消删除零变更、右键多选文件与非空目录删除。原上传冲突/深层刷新两个 fixture 用例亦通过。25秒外部 watchdog，菜单驱动另有3秒截止，零远端 exec、零残留 `.part`。
- 真实生产日志顺序断言：UploadDone → DeleteAccepted → DeleteSubmit → DeleteBegin → DeleteDone → DeleteUiBegin → RemoveBegin/End → DeleteUiEnd → RenderBegin/Sort/Filter/Layout/End；JSONL复制到报告供复核。安全断言无 fixture密码、文件名或host原文。
- **人工诊断测试**主动阻塞 GUI 3.2秒，不处理Qt事件；后台在GUI恢复前已落盘 RemoveBegin 与 UiStall（含最后阶段/job及≥2秒年龄）。这不是用户 Hang 复现。另验证队列溢出、3份小上限轮转、字段白名单、停止timer/线程与停止后cheap no-op。设置复制路径不读取凭据。
- 暗浅实际Qt截图 `file-to-parent-dark/light.png`、`root-file-to-path-dark/light.png`；子树展开/收起、hidden、同级边界、离屏裁剪、render清除pathframe均有断言。截图灰色文件行为既有选择，不是上传目标边框。

证据位于 `test-results/0.1.21/`：`ctest-final.txt`、各 `*-final.txt`、`sftp-integration.txt`、`sftp-audit.jsonl`、`fixture-diagnostic.jsonl`、`artificial-ui-stall.jsonl`。最初 selected/current、深滚动与实际上传删除检查亦通过；报告中带 red 的早期文件名不代表成功复现失败。

## 交付边界

新目录 `dist/RabbitShell-0.1.21` 与 `dist/RabbitShell-0.1.21-portable.exe`，旧 .12–.20 共18目标646文件按开工前路径/size/SHA inventory核对。最终app重新链接，文档冻结同步目录，再pin approved inventory，显式 `-Version 0.1.21` 生成portable。本文不预写包装通过；目录验证、PE静态与最小PATH/中文空格/私有profile/仅EXE的version、manager、demo三项烟测，实际app/portable/payload SHA及最终源清单见 `final-report.json` / `source-files.json`。

无真实服务器/用户凭据/信任文件/可见桌面操作；真实Explorer鼠标、混合DPI及非Windows构建未在本轮验收。隔离链路正常不能排除用户实机的GUI Hang；若复发需据日志进一步定位。
