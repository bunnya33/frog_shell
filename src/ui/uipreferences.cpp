#include "uipreferences.h"
#include "contentfont.h"
#include "uizoom.h"
#include "theme.h"
namespace rs {
UiPreferences &UiPreferences::instance() { static UiPreferences value; return value; }
void UiPreferences::reset() {
    if (restoring) return;
    QScopedValueRollback<bool> guard(restoring,true);
    emit aboutToReset();
    QSettings settings;
    for (auto group : {"appearance","process-table","listening-table","services","network","files","downloads","window","editor","workspace-layout"})
        settings.remove(group);
    ContentFonts::instance().reload();
    UiZoom::instance().reload();
    applyTheme(false);
    emit defaultsRestored();
}
}
