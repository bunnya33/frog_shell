#pragma once
#include <QtWidgets>

namespace rs {
// Dedicated to the asynchronous transfer-conflict flow. The worker and its
// lifetime remain owned by Workspace; this class only collects a choice.
class UploadConflictDialog final : public QDialog {
  public:
    UploadConflictDialog(const QString &title, const QString &path, bool moving,
                         bool canOverwrite, QWidget *parent);
    QList<QAbstractButton *> buttons() const { return {overwrite, keep, cancel}; }
    QPushButton *defaultButton() const { return cancel; }
    QCheckBox *checkBox() const { return all; }
    QAbstractButton *clickedButton() const { return clicked; }
  private:
    QPushButton *overwrite, *keep, *cancel;
    QCheckBox *all;
    QAbstractButton *clicked = nullptr;
};
}
