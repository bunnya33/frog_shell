# libvterm 0.3.3 local screen-resize patch

Upstream: https://www.leonerd.org.uk/code/libvterm/libvterm-0.3.3.tar.gz

Archive SHA256: `09156f43dd2128bd347cbeebe50d9a571d32c64e0cf18d211197946aff7226e0`.
Original `src/state.c` SHA256: `8f6504f091a91194fa4e972a142d586fb96bd8e7a15b6d26747323f1fe48ff38`.
Original `src/screen.c` SHA256: `d37cc2977595c9766dd85d20ce86e3a7838271a7df9c28f9ff1ac4a8ff1bb59a`.

Copyright (c) 2008 Paul Evans <leonerd@leonerd.org.uk>. MIT permission and warranty disclaimer are preserved in `third_party/licenses/libvterm.txt`, distributed as `licenses/libvterm.txt`.

RabbitShell 0.1.16 applies `cmake/PatchLibvterm.cmake` to build-directory copies of `screen.c` and `state.c`. CMake verifies both original hashes and every replacement anchor. The fetched source is unchanged; no Python, Git patch tool, new runtime library or dependency upgrade is used.

The patch limits reflow to the primary buffer, counts and copies wide glyphs as indivisible pairs (retaining combining codepoints and pen attributes), leaves only unused blank padding at wrap boundaries, reads primary cells for history even while alternate screen is active, initializes popped line metadata, and preserves the mapped cursor during backfill. Oversized groups stream overflow in order through one scratch row, retaining the visible tail. The small state change preserves resize/DEC saved pending wrap with a one-past-last saved column and no structure/API change. Alternate-screen resizing keeps the original fixed-grid behavior.

The application separately splits oversized history rows at glyph boundaries, pads pop requests with width=1/default colors, keeps the existing 2000-row/2MiB bounds and maps selections by glyph ordinal. Widening does not merge historical physical lines. Verification and limitations are recorded in the 0.1.16 release record.

RabbitShell 0.1.26 appends two local screen functions to the same hash-guarded
generated `screen.c`, declared only by `src/ui/vterm_extensions.h`. The query finds
the cursor's visible logical-line boundaries using continuation metadata. The clear
function blanks other primary cells/line metadata with default-color empty cells
and reports screen damage. It does not move cells/cursor or change saved cursor,
pen, modes, scroll region, pending wrap, UTF-8/CSI parser or keyboard state. The
application then clears its own history/selection. No escape sequences are injected
and no PTY input is sent. Alternate screen and a line crossing the top into history
are refused. Historical physical rows lack reliable logical-boundary metadata;
this patch does not guess a prefix or redesign history. Hard-newline shell syntax
cannot be inferred without shell integration. All original 0.1.16 resize/reflow
patches above remain in place; upstream files and public vterm.h are unchanged.

The public 0.3.3 screen API offers cell reads/reset and damage callbacks, but no
cell-preserving erase primitive. The prior local `ED2 + CUP home` erased the prompt
and changed cursor coordinates while the remote line editor received no update.
The 0.1.26 red/green tests demonstrate that defect and retained relative editing,
parser fragments, colors, wrapping and DEC save/restore behavior. Synthetic
full-screen/history/resize tests are not a claim of reproducing the user's exact
Vim suspend/exit issue or of testing Vim in a real PTY.

FrogShell 0.1.40 retains those pinned upstream hashes and anchor checks, and adds
an instance-local primary-resize bridge declared only in vterm_extensions.h.
Before state.c shifts lineinfo on a scroll, a bounded screen-owned scratch array
captures the outgoing continuation bits. The existing push callback reads the
corresponding metadata while cells still belong to that row. No full-width or
trailing-space heuristic determines a logical boundary.

Terminal now stores cells, lineinfo and a sparse protected-cell bit vector per
history row. It consumes history and primary screen as one stream, including a
logical line crossing their seam, and rewraps at glyph boundaries. Hard newlines,
blank records, actual U+0020 trailing spaces, combining codepoints and pen
attributes remain data. Only unused null padding is omitted. DECSCA's internal
protected_cell bit is explicitly exported/imported because public
VTermScreenCellAttrs does not contain it.

A synchronous row provider supplies only the bounded new primary-screen tail to
the native resize transaction; cursor, pending wrap, the active combining anchor
and inactive primary saved cursor are mapped in the same transaction. The native
alternate-buffer branch remains fixed-grid. No callback is global, and its
borrowed provider pointer is cleared before resize returns. Public vterm.h and
the fetched upstream files remain unchanged.

History is still limited to 2000 rows / 2MiB, now including row metadata and bit
vectors. Reflow consumes the old deque, emits capped history and keeps at most
one new screen tail plus one working row; there is no growing transcript cache.
Selection and scroll anchors map by retained glyph ordinal. The existing clear
guard still refuses to cut a cursor logical line crossing the screen top; the
application does not inject VT output, erase history to hide a resize problem,
restart a shell or request any server-side tool. The original 0.1.16 statement
above that widening cannot merge historical physical lines describes the old
implementation and is superseded by this bridge.
