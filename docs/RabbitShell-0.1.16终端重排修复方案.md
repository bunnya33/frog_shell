# 0.1.16 终端缩放重排：实现与验证

状态：主审通过方案后已实现。初始失败证据为 `test-results/0.1.16/resize-reflow-red.txt`：未启用 reflow 丢右侧字符，直接启用原生 reflow 拆开中文 lead/continuation。最终 appearance **23/0/0**，受影响 CTest **7/7**。包外交付结果见 `test-results/0.1.16/final-report.json`。

## 来源与构建门禁

保持 libvterm 0.3.3、原 MIT 许可，不升级整库。原 archive SHA256：`09156f43dd2128bd347cbeebe50d9a571d32c64e0cf18d211197946aff7226e0`。

| 原文件 | SHA256 |
| --- | --- |
| src/screen.c | d37cc2977595c9766dd85d20ce86e3a7838271a7df9c28f9ff1ac4a8ff1bb59a |
| src/state.c | 8f6504f091a91194fa4e972a142d586fb96bd8e7a15b6d26747323f1fe48ff38 |

`cmake/PatchLibvterm.cmake` 校验两个原文件及每个替换片段，生成 build 下 patched 源参与编译；FetchContent 原文件不改，不依赖 Python/Git patch。源或片段不符即配置失败。版权、来源与变更说明见 `third_party/patches/libvterm-0.3.3.md` 和原 MIT 许可。

## 主屏重排与光标

- 仅 primary buffer 启用 reflow；alternate buffer 继续原生固定网格裁切。
- 主屏保留原分组/留白/scrollback 框架，同一套字形规则计算行高和搬运：宽字完整成对，组合码点及 pen 属性随 cell 保留；新行不足两格先留未使用 padding。仅剔除尾部未写入的 chars[0]==0，实际 U+0020 空格不丢。
- 历史回填只占空余行，已有正文/光标不移动，不额外加回填行数；只在最后整体上移时减去剩余空行。lineinfo 初始化，不读未初始化 continuation。
- 单记录重排高度超过新屏幕时，先推之前原行，再以单行 scratch 顺序推送本组溢出；屏幕保留尾部继续输出。不整组只推首行，也不在空屏 clamp 后覆盖正文。alt 活跃时仍从 primary buffer 取历史。
- inactive primary 同步映射 DEC saved cursor。极小 state 补丁用 saved column 的 one-past-last 编码 pending wrap，恢复时还原，不改结构/API；resize 的 updatecursor 不清除映射后的 wrap。终端不重建，不回放协议，不重发回应。

## 历史与选区

历史只存 cells，缩窄拆分过宽物理行且不拆宽字；放大**不自动合并历史物理行**，保留软分行这一边界。新历史立即执行既有 **2000 行 / 2MiB cell bytes** 上限，重排至多短暂保留两个有界 deque 和一行 scratch，无无限 rawlog。

sb_popline 不消费会被 old/new grid 截断的行；空位填 width=1 和合法 DEFAULT_FG/BG，避免 width=0 死循环。选区端点按字形序数映射，组合码点属于一个 cell、wide continuation 不独立计数，扣除淘汰字形数。端点淘汰则清选；不搜索重复字串。复制跳过未使用 padding，当前主屏软换行不插假空格，历史物理分行保留。

正常尾部、屏幕/历史实际文本选区已覆盖。未写入尾部空白不作为字形端点；实际输出空格计数。若绝对坐标应用将光标放在重排后已进入历史的正文，光标 clamp 到保留区，不承诺恢复不可见的编辑位置。alt 不承诺保留固定网格右侧裁切内容。

## 实际验证

- 长行接近原列宽、中文在新边界前/正跨边界/后、组合字符、真实尾空格、缩放往返；比较 cells 的有序逻辑内容，另验证继续输入、选区字串和 IME rect，不能仅去换行掩盖覆盖。
- 屏幕与 history 分别验证宽字；短行回填 padding、超宽历史拒绝消费、回填后继续输入；历史行数/bytes 与端点超限清除。
- 精确满列及 phantom wrap、active 主屏 / alt 活跃的 inactive primary 溢出、返回主屏后继续输出。
- 两行小高度窗口超屏逻辑记录、缩窄后及恢复后继续输出。初始真实丢字见 oversized-red.txt；修复后的定向8/0/0及完整 appearance23/0/0保留。

第一轮光标专项5失败见 resize-targeted-initial.txt：满列覆盖、回填假空格、alt saved cursor。修复后第二阶段7/0/0；超屏缺陷再修复后8/0/0；最终以 appearance-Release.txt 23/0/0 为准。不把中间结果当最终结论。

Windows offscreen、临时配置；没有真实远端 PTY/full-screen 应用、真实桌面、跨屏/DPI或 macOS/Linux 实机验收。内存仅有界设计与边界检查，本轮未重测整体压力矩阵。
