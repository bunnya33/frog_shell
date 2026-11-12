# Narrow local fix for libvterm 0.3.3 screen resize, MIT licensed upstream.
# Copyright (c) 2008 Paul Evans <leonerd@leonerd.org.uk>
# See third_party/licenses/libvterm.txt and third_party/patches/libvterm-0.3.3.md.
file(SHA256 "${libvterm_SOURCE_DIR}/src/screen.c" screen_hash)
if(NOT screen_hash STREQUAL "d37cc2977595c9766dd85d20ce86e3a7838271a7df9c28f9ff1ac4a8ff1bb59a")
  message(FATAL_ERROR "libvterm 0.3.3 screen.c differs from pinned source; resize patch requires review")
endif()
file(READ "${libvterm_SOURCE_DIR}/src/screen.c" patched_screen)
macro(screen_replace old new)
  string(LENGTH "${patched_screen}" before_length)
  string(LENGTH "${old}" fragment_length)
  string(REPLACE "${old}" "" without_fragment "${patched_screen}")
  string(LENGTH "${without_fragment}" after_length)
  math(EXPR removed_length "${before_length}-${after_length}")
  if(NOT removed_length EQUAL fragment_length)
    message(FATAL_ERROR "libvterm resize patch anchor missing or duplicated")
  endif()
  string(REPLACE "${old}" "${new}" patched_screen "${patched_screen}")
endmacro()
set(old [=[#include "vterm_internal.h"]=])
set(new [=[#include "vterm_internal.h"
#include "@CMAKE_CURRENT_SOURCE_DIR@/src/ui/vterm_extensions.h"]=])
screen_replace("${old}" "${new}")
set(old [=[  ScreenPen pen;
};]=])
set(new [=[  ScreenPen pen;
  const RabbitShellVTermResize *rabbit_resize;
  VTermLineInfo rabbit_pushed_info;
  VTermLineInfo *rabbit_outgoing;
  int rabbit_outgoing_capacity, rabbit_outgoing_count;
  int rabbit_pushed_row;
};]=])
screen_replace("${old}" "${new}")
set(old [=[  (screen->callbacks->sb_pushline)(screen->cols, screen->sb_buffer, screen->cbdata);]=])
set(new [=[  screen->rabbit_pushed_info = row < screen->rabbit_outgoing_count
    ? screen->rabbit_outgoing[row] : screen->state->lineinfos[BUFIDX_PRIMARY][row];
  screen->rabbit_pushed_row = row;
  (screen->callbacks->sb_pushline)(screen->cols, screen->sb_buffer, screen->cbdata);]=])
screen_replace("${old}" "${new}")
set(old [=[    for(int row = 0; row < src.start_row; row++)
      sb_pushline_from_row(screen, row);]=])
set(new [=[    for(int row = 0; row < src.start_row; row++)
      sb_pushline_from_row(screen, row);
    screen->rabbit_outgoing_count = 0;]=])
screen_replace("${old}" "${new}")
set(old [=[  screen->reflow = false;]=])
set(new [=[  screen->reflow = false;
  screen->rabbit_resize = NULL;
  screen->rabbit_pushed_info = (VTermLineInfo){ 0 };
  screen->rabbit_outgoing = NULL;
  screen->rabbit_outgoing_capacity = screen->rabbit_outgoing_count = 0;]=])
screen_replace("${old}" "${new}")
set(old [=[  vterm_allocator_free(screen->vt, screen);
}]=])
set(new [=[  if(screen->rabbit_outgoing) vterm_allocator_free(screen->vt, screen->rabbit_outgoing);
  vterm_allocator_free(screen->vt, screen);
}]=])
screen_replace("${old}" "${new}")
set(old [=[  VTermLineInfo *new_lineinfo = vterm_allocator_malloc(screen->vt, sizeof(new_lineinfo[0]) * new_rows);

  int old_row = old_rows - 1;]=])
set(new [=[  VTermLineInfo *new_lineinfo = vterm_allocator_malloc(screen->vt, sizeof(new_lineinfo[0]) * new_rows);

  /* The application streams primary history + screen as one logical stream.
   * Import its bounded tail once. Alternate screen keeps the native grid path. */
  if(bufidx == BUFIDX_PRIMARY && screen->rabbit_resize) {
    const RabbitShellVTermResize *prepared = screen->rabbit_resize;
    for(int row = 0; row < new_rows; row++) {
      VTermLineInfo info = { 0 };
      prepared->row(row, new_cols, screen->sb_buffer, &info, prepared->user);
      new_lineinfo[row] = info;
      for(int col = 0; col < new_cols; col++) {
        VTermScreenCell *src = &screen->sb_buffer[col];
        ScreenCell *dst = &new_buffer[row * new_cols + col];
        memset(dst, 0, sizeof(*dst));
        memcpy(dst->chars, src->chars, sizeof(dst->chars));
        dst->pen.bold = src->attrs.bold; dst->pen.underline = src->attrs.underline;
        dst->pen.italic = src->attrs.italic; dst->pen.blink = src->attrs.blink;
        dst->pen.reverse = src->attrs.reverse ^ screen->global_reverse;
        dst->pen.conceal = src->attrs.conceal; dst->pen.strike = src->attrs.strike;
        dst->pen.font = src->attrs.font; dst->pen.small = src->attrs.small;
        dst->pen.baseline = src->attrs.baseline; dst->pen.dwl = src->attrs.dwl;
        dst->pen.dhl = src->attrs.dhl; dst->pen.fg = src->fg; dst->pen.bg = src->bg;
        dst->pen.protected_cell = prepared->protected_cell && prepared->protected_cell(row, col, prepared->user);
      }
    }
    vterm_allocator_free(screen->vt, old_buffer);
    vterm_allocator_free(screen->vt, old_lineinfo);
    screen->buffers[bufidx] = new_buffer;
    statefields->lineinfos[bufidx] = new_lineinfo;
    if(active) {
      statefields->pos = prepared->cursor;
      screen->state->at_phantom = prepared->phantom;
      if(prepared->map_combine) screen->state->combine_pos = prepared->combine;
    }
    else {
      screen->state->saved.pos = prepared->cursor;
      if(prepared->phantom) screen->state->saved.pos.col++;
    }
    screen->rabbit_resize = NULL;
    return;
  }

  int old_row = old_rows - 1;]=])
screen_replace("${old}" "${new}")
set(old [=[#define REFLOW (screen->reflow)]=])
set(new [=[/* RabbitShell: only the primary screen is reflowed. Full-screen apps keep
 * the upstream fixed-grid alternate-screen resize semantics. */
#define REFLOW (screen->reflow && bufidx == BUFIDX_PRIMARY)

/* Count and copy with the same indivisible-glyph rule. A trailing unused cell
 * before a wide glyph is padding, whereas an actual U+0020 is content. */
static int reflow_glyph_width(ScreenCell *buffer, int row, int col, int used, int cols)
{
  return col + 1 < used && buffer[row * cols + col + 1].chars[0] == (uint32_t)-1 ? 2 : 1;
}

static int reflow_height(ScreenCell *buffer, int first, int last, int rows, int cols, int new_cols)
{
  int height = 1, column = 0;
  for(int row = first; row <= last; row++) {
    int used = line_popcount(buffer, row, rows, cols);
    for(int col = 0; col < used;) {
      int width = reflow_glyph_width(buffer, row, col, used, cols);
      if(column + width > new_cols) { height++; column = 0; }
      column += width;
      col += width;
    }
  }
  return height;
}]=])
screen_replace("${old}" "${new}")
set(old [=[    if(new_row_start < 0) {]=])
set(new [=[    if(new_row_start < 0 && !REFLOW) {]=])
screen_replace("${old}" "${new}")
set(old [=[    for(new_row = new_row_start, old_row = old_row_start; new_row <= new_row_end; new_row++) {]=])
set(new [=[    /* A logical record can be taller than the new screen. Stream only
     * the overflow through one scratch row, retaining its tail and cursor on
     * screen. Push preceding old rows first to preserve chronological order. */
    int spilling = REFLOW && new_row_start < 0;
    ScreenCell *spill_row = spilling ? vterm_allocator_malloc(screen->vt, sizeof(ScreenCell) * new_cols) : NULL;
    if(spilling && screen->callbacks && screen->callbacks->sb_pushline) {
      ScreenCell *active_buffer = screen->buffer;
      screen->buffer = old_buffer;
      for(int row = 0; row < old_row_start; row++) sb_pushline_from_row(screen, row);
      screen->buffer = active_buffer;
    }
    for(new_row = new_row_start, old_row = old_row_start; new_row <= new_row_end; new_row++) {
      ScreenCell *destination = new_row < 0 ? spill_row : &new_buffer[new_row * new_cols];]=])
screen_replace("${old}" "${new}")
set(old [=[  VTermPos old_cursor = statefields->pos;
  VTermPos new_cursor = { -1, -1 };]=])
set(new [=[  VTermPos old_cursor = !active && REFLOW ? screen->state->saved.pos : statefields->pos;
  VTermPos new_cursor = { -1, -1 };
  int new_phantom = screen->state->at_phantom;]=])
screen_replace("${old}" "${new}")
set(old [=[    while(REFLOW && old_lineinfo && old_row >= 0 && old_lineinfo[old_row].continuation)]=])
set(new [=[    while(REFLOW && old_lineinfo && old_row > 0 && old_lineinfo[old_row].continuation)]=])
screen_replace("${old}" "${new}")
set(old [=[    int new_height = REFLOW
      ? width ? (width + new_cols - 1) / new_cols : 1
      : 1;]=])
set(new [=[    int new_height = REFLOW
      ? reflow_height(old_buffer, old_row_start, old_row_end, old_rows, old_cols, new_cols)
      : 1;]=])
screen_replace("${old}" "${new}")
set(old [=[      if(old_row_start <= old_cursor.row && old_cursor.row < old_row_end) {]=])
set(new [=[      if(old_row_start <= old_cursor.row && old_cursor.row <= old_row_end) {]=])
screen_replace("${old}" "${new}")
set(old [=[      while(count) {
        /* TODO: This could surely be done a lot faster by memcpy()'ing the entire range */]=])
set(new [=[      if(REFLOW) {
        while(old_row <= old_row_end) {
          int used = line_popcount(old_buffer, old_row, old_rows, old_cols);
          if(old_col >= used) {
            if(old_cursor.row == old_row && old_cursor.col >= old_col) {
              new_cursor.row = new_row;
              new_cursor.col = new_col + old_cursor.col - old_col;
              if(new_cursor.col >= new_cols) {
                if(active) { new_phantom = 1; new_cursor.col = new_cols - 1; }
                else new_cursor.col = new_cols; /* saved cursor encodes pending wrap */
              }
            }
            old_row++;
            old_col = 0;
            continue;
          }
          int glyph_width = reflow_glyph_width(old_buffer, old_row, old_col, used, old_cols);
          if(new_col + glyph_width > new_cols) break;
          for(int i = 0; i < glyph_width; i++) {
            destination[new_col + i] = old_buffer[old_row * old_cols + old_col + i];
            if(old_cursor.row == old_row && old_cursor.col == old_col + i)
              new_cursor.row = new_row, new_cursor.col = new_col + i;
          }
          old_col += glyph_width;
          new_col += glyph_width;
        }
      }
      else while(count) {
        /* TODO: This could surely be done a lot faster by memcpy()'ing the entire range */]=])
screen_replace("${old}" "${new}")
set(old [=[        clearcell(screen, &new_buffer[new_row * new_cols + new_col]);]=])
set(new [=[        clearcell(screen, &destination[new_col]);]=])
screen_replace("${old}" "${new}")
# This anchor also appears in the new reflow branch; use the full following text.
set(old [=[      if(old_cursor.row == old_row && old_cursor.col >= old_col) {
        new_cursor.row = new_row, new_cursor.col = (old_cursor.col - old_col + new_col);]=])
set(new [=[      if(!REFLOW && old_cursor.row == old_row && old_cursor.col >= old_col) {
        new_cursor.row = new_row, new_cursor.col = (old_cursor.col - old_col + new_col);]=])
screen_replace("${old}" "${new}")
set(old [=[      new_lineinfo[new_row].continuation = (new_row > new_row_start);]=])
set(new [=[      new_lineinfo[new_row] = (VTermLineInfo){ .continuation = (new_row > new_row_start) };]=])
screen_replace("${old}" "${new}")
set(old [=[      new_lineinfo[new_row] = (VTermLineInfo){ .continuation = (new_row > new_row_start) };
    }

    old_row = old_row_start - 1;]=])
set(new [=[      if(new_row >= 0)
        new_lineinfo[new_row] = (VTermLineInfo){ .continuation = (new_row > new_row_start) };
      else if(screen->callbacks && screen->callbacks->sb_pushline) {
        VTermScreen one_row = *screen;
        one_row.rows = 1;
        one_row.cols = new_cols;
        one_row.buffer = spill_row;
        sb_pushline_from_row(&one_row, 0);
      }
    }
    if(spilling) {
      vterm_allocator_free(screen->vt, spill_row);
      if(new_cursor.row < 0) new_cursor.row = 0, new_cursor.col = 0;
      old_row = new_row = -1;
      break;
    }

    old_row = old_row_start - 1;]=])
screen_replace("${old}" "${new}")
set(old [=[    if(screen->callbacks && screen->callbacks->sb_pushline)
      for(int row = 0; row <= old_row; row++)
        sb_pushline_from_row(screen, row);]=])
set(new [=[    if(screen->callbacks && screen->callbacks->sb_pushline) {
      ScreenCell *active_buffer = screen->buffer;
      screen->buffer = old_buffer;
      for(int row = 0; row <= old_row; row++)
        sb_pushline_from_row(screen, row);
      screen->buffer = active_buffer;
    }]=])
screen_replace("${old}" "${new}")
set(old [=[      VTermPos pos = { .row = new_row };
      for(pos.col = 0; pos.col < old_cols]=])
set(new [=[      VTermPos pos = { .row = new_row };
      new_lineinfo[new_row] = (VTermLineInfo){ 0 };
      for(pos.col = 0; pos.col < old_cols]=])
screen_replace("${old}" "${new}")
set(old [=[      if(active)
        statefields->pos.row++;]=])
set(new [=[      /* Backfill occupies free rows; existing content/cursor did not move. */]=])
screen_replace("${old}" "${new}")
set(old [=[  if(active)
    statefields->pos = new_cursor;]=])
set(new [=[  if(active) {
    statefields->pos = new_cursor;
    if(REFLOW) screen->state->at_phantom = new_phantom;
  }
  else if(REFLOW)
    screen->state->saved.pos = new_cursor;]=])
screen_replace("${old}" "${new}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/patched-libvterm")
set(patched_screen_path "${CMAKE_CURRENT_BINARY_DIR}/patched-libvterm/screen.c")
string(APPEND patched_screen [=[

void rabbitshell_vterm_screen_prepare_resize(VTermScreen *screen, const RabbitShellVTermResize *prepared)
{
  screen->rabbit_resize = prepared;
}
int rabbitshell_vterm_screen_primary_cell(const VTermScreen *screen, VTermPos pos, VTermScreenCell *cell)
{
  VTermScreen primary = *screen;
  primary.buffer = screen->buffers[BUFIDX_PRIMARY];
  return vterm_screen_get_cell(&primary, pos, cell);
}
int rabbitshell_vterm_screen_primary_protected(const VTermScreen *screen, VTermPos pos)
{
  if(pos.row < 0 || pos.row >= screen->rows || pos.col < 0 || pos.col >= screen->cols) return 0;
  return screen->buffers[BUFIDX_PRIMARY][pos.row * screen->cols + pos.col].pen.protected_cell;
}
int rabbitshell_vterm_screen_pushed_protected(const VTermScreen *screen, int column)
{
  ScreenCell *cell = getcell(screen, screen->rabbit_pushed_row, column);
  return cell ? cell->pen.protected_cell : 0;
}
const VTermLineInfo *rabbitshell_vterm_screen_primary_lineinfo(const VTermScreen *screen, int row)
{
  return row >= 0 && row < screen->rows ? &screen->state->lineinfos[BUFIDX_PRIMARY][row] : NULL;
}
void rabbitshell_vterm_screen_primary_cursor(const VTermScreen *screen, VTermPos *cursor, int *phantom, VTermPos *combine)
{
  int active = screen->buffer == screen->buffers[BUFIDX_PRIMARY];
  *cursor = active ? screen->state->pos : screen->state->saved.pos;
  *phantom = active ? screen->state->at_phantom : cursor->col >= screen->cols;
  if(*phantom) cursor->col = screen->cols - 1;
  *combine = active ? screen->state->combine_pos : (VTermPos){ -1, -1 };
}
const VTermLineInfo *rabbitshell_vterm_screen_pushed_lineinfo(const VTermScreen *screen)
{
  return &screen->rabbit_pushed_info;
}
void rabbitshell_vterm_capture_history(VTerm *vt, VTermRect rect, int downward, int rightward)
{
  VTermScreen *screen = vt->screen;
  if(!screen) return;
  screen->rabbit_outgoing_count = 0;
  if(downward <= 0 || rightward || rect.start_row || rect.start_col || rect.end_col != screen->cols ||
      screen->buffer != screen->buffers[BUFIDX_PRIMARY]) return;
  if(screen->rabbit_outgoing_capacity < downward) {
    if(screen->rabbit_outgoing) vterm_allocator_free(vt, screen->rabbit_outgoing);
    screen->rabbit_outgoing = vterm_allocator_malloc(vt, sizeof(VTermLineInfo) * downward);
    screen->rabbit_outgoing_capacity = downward;
  }
  memcpy(screen->rabbit_outgoing, screen->state->lineinfos[BUFIDX_PRIMARY], sizeof(VTermLineInfo) * downward);
  screen->rabbit_outgoing_count = downward;
}

/* RabbitShell local clear: preserve the complete visible cursor logical line,
 * including cells, attributes, coordinates, pending wrap and parser state.
 * No VT injection/replay and no relocation of the remote editor's cursor. */
static int rabbitshell_clear_bounds(const VTermScreen *screen, int *first, int *end)
{
  if(screen->buffer != screen->buffers[BUFIDX_PRIMARY]) return -1;
  *first = screen->state->pos.row;
  *end = *first + 1;
  while(*first > 0 && screen->state->lineinfo[*first].continuation) --*first;
  /* Physical history rows do not retain logical-boundary metadata. Never
   * guess and cut off an input prefix whose first row is no longer visible. */
  if(*first == 0 && screen->state->lineinfo[0].continuation) return 0;
  while(*end < screen->rows && screen->state->lineinfo[*end].continuation) ++*end;
  return 1;
}
int rabbitshell_vterm_screen_can_clear_output(const VTermScreen *screen)
{
  int first, end;
  return rabbitshell_clear_bounds(screen, &first, &end);
}
int rabbitshell_vterm_screen_clear_output(VTermScreen *screen)
{
  int first, end;
  int result = rabbitshell_clear_bounds(screen, &first, &end);
  if(result != 1) return result;
  vterm_screen_flush_damage(screen);
  VTermColor fg, bg;
  vterm_state_get_default_colors(screen->state, &fg, &bg);
  fg.type = (fg.type & ~VTERM_COLOR_DEFAULT_MASK) | VTERM_COLOR_DEFAULT_FG;
  bg.type = (bg.type & ~VTERM_COLOR_DEFAULT_MASK) | VTERM_COLOR_DEFAULT_BG;
  for(int row = 0; row < screen->rows; ++row) {
    if(row >= first && row < end) continue;
    screen->state->lineinfo[row] = (VTermLineInfo){ 0 };
    for(int col = 0; col < screen->cols; ++col) {
      ScreenCell *cell = getcell(screen, row, col);
      memset(cell, 0, sizeof(*cell));
      cell->pen.fg = fg;
      cell->pen.bg = bg;
    }
  }
  damagescreen(screen);
  return 1;
}
]=])
file(CONFIGURE OUTPUT "${patched_screen_path}" CONTENT "${patched_screen}" @ONLY)
list(REMOVE_ITEM VTERM_SOURCES "${libvterm_SOURCE_DIR}/src/screen.c")
list(APPEND VTERM_SOURCES "${patched_screen_path}")

# The state layer must not cancel the resize-mapped pending wrap. Its saved
# cursor uses one-past-last-column to retain that state across DEC 1048/1049,
# without changing a public/internal structure or the parser protocol.
file(SHA256 "${libvterm_SOURCE_DIR}/src/state.c" state_hash)
if(NOT state_hash STREQUAL "8f6504f091a91194fa4e972a142d586fb96bd8e7a15b6d26747323f1fe48ff38")
  message(FATAL_ERROR "libvterm 0.3.3 state.c differs from pinned source; cursor patch requires review")
endif()
file(READ "${libvterm_SOURCE_DIR}/src/state.c" patched_screen)
set(old [=[#include "vterm_internal.h"]=])
set(new [=[#include "vterm_internal.h"
#include "@CMAKE_CURRENT_SOURCE_DIR@/src/ui/vterm_extensions.h"]=])
screen_replace("${old}" "${new}")
set(old [=[  // Update lineinfo if full line]=])
set(new [=[  rabbitshell_vterm_capture_history(state->vt, rect, downward, rightward);

  // Update lineinfo if full line]=])
screen_replace("${old}" "${new}")
set(old [=[    state->saved.pos = state->pos;
    state->saved.mode.cursor_visible]=])
set(new [=[    state->saved.pos = state->pos;
    if(state->at_phantom) state->saved.pos.col++;
    state->saved.mode.cursor_visible]=])
screen_replace("${old}" "${new}")
set(old [=[    state->pos = state->saved.pos;

    settermprop_bool]=])
set(new [=[    state->pos = state->saved.pos;
    int restored_phantom = state->pos.col >= state->cols;
    if(restored_phantom) state->pos.col = state->cols - 1;

    settermprop_bool]=])
screen_replace("${old}" "${new}")
set(old [=[    updatecursor(state, &oldpos, 1);
  }
}

static int on_escape]=])
set(new [=[    updatecursor(state, &oldpos, 1);
    state->at_phantom = restored_phantom;
  }
}

static int on_escape]=])
screen_replace("${old}" "${new}")
set(old [=[  updatecursor(state, &oldpos, 1);

  return 1;
}

static const VTermParserCallbacks parser_callbacks]=])
set(new [=[  updatecursor(state, &oldpos, 0);

  return 1;
}

static const VTermParserCallbacks parser_callbacks]=])
screen_replace("${old}" "${new}")
set(patched_state_path "${CMAKE_CURRENT_BINARY_DIR}/patched-libvterm/state.c")
file(CONFIGURE OUTPUT "${patched_state_path}" CONTENT "${patched_screen}" @ONLY)
list(REMOVE_ITEM VTERM_SOURCES "${libvterm_SOURCE_DIR}/src/state.c")
list(APPEND VTERM_SOURCES "${patched_state_path}")
