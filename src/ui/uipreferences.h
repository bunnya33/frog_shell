#pragma once
#include <QObject>
namespace rs {
// Only local presentation preferences; connection/credential storage is separate.
class UiPreferences final : public QObject {
    Q_OBJECT
  public:
    static UiPreferences &instance();
    bool resetting() const { return restoring; }
    void reset();
  signals:
    void aboutToReset();
    void defaultsRestored();
  private:
    bool restoring = false;
};
}
