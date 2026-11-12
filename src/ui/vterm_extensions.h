#pragma once
#include <vterm.h>
// Local, hash-guarded screen extension, not an escape sequence sent to the PTY.
#ifdef __cplusplus
extern "C" {
#endif
// 1: whole cursor logical line is on primary screen; 0: crosses retained history;
// -1: alternate screen. The query never mutates screen/state/parser.
int rabbitshell_vterm_screen_can_clear_output(const VTermScreen *screen);
int rabbitshell_vterm_screen_clear_output(VTermScreen *screen);
// Private per-screen bridge. The prepared row provider lives only for the
// synchronous vterm_set_size call; no global callback or public ABI is changed.
typedef struct {
    int (*row)(int row, int columns, VTermScreenCell *cells, VTermLineInfo *info, void *user);
    int (*protected_cell)(int row, int column, void *user);
    void *user;
    VTermPos cursor;
    VTermPos combine;
    int phantom, map_combine;
} RabbitShellVTermResize;
void rabbitshell_vterm_screen_prepare_resize(VTermScreen *, const RabbitShellVTermResize *);
int rabbitshell_vterm_screen_primary_cell(const VTermScreen *, VTermPos, VTermScreenCell *);
int rabbitshell_vterm_screen_primary_protected(const VTermScreen *, VTermPos);
int rabbitshell_vterm_screen_pushed_protected(const VTermScreen *, int column);
const VTermLineInfo *rabbitshell_vterm_screen_primary_lineinfo(const VTermScreen *, int row);
void rabbitshell_vterm_screen_primary_cursor(const VTermScreen *, VTermPos *, int *phantom, VTermPos *combine);
const VTermLineInfo *rabbitshell_vterm_screen_pushed_lineinfo(const VTermScreen *);
// Called before state.c moves lineinfo, so outgoing history keeps its real
// continuation bit even when the scroll moves the last row off the screen.
void rabbitshell_vterm_capture_history(VTerm *, VTermRect, int downward, int rightward);
#ifdef __cplusplus
}
#endif
