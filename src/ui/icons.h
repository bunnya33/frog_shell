#pragma once
#include <QtWidgets>
namespace rs {
enum class Icon {
    Add, NewFile, NewFolder, Back, Forward, Up, Down, Refresh, Home, Path,
    SplitHorizontal, SplitVertical, Merge, Terminal, Stop, Collapse, Transfers, ChevronRight, ChevronDown, ReplaceAll,
    Settings, Status, Explorer, Close, Return, Expand, Restore, Replace, Import, Connection,
    Trash, PanelMaximize, PanelRestore, More,
    Upload, DownloadFile, DownloadFolder, Rename, Info, Eye, CalendarClock,
    Cut, Copy, Paste, Undo, BookmarkPlus, Save, Lock, Grip
};
enum class IconSize { Toolbar, Activity, TabClose };
QIcon icon(Icon glyph);
QIcon resourceMenuIcon(Icon glyph);
void replaceTabButton(QTabBar *bar, int index, QTabBar::ButtonPosition side, QWidget *replacement);
QToolButton *iconButton(Icon glyph, const QString &name, QWidget *parent = nullptr,
                        IconSize size = IconSize::Toolbar, const QString &shortcut = {});
} // namespace rs
