#pragma once
#include <QtWidgets>
namespace rs {
class StyledComboBox final : public QComboBox {
    Q_OBJECT
  public:
    explicit StyledComboBox(QWidget *parent = nullptr);
    void showPopup() override;
    void hidePopup() override;

  protected:
    void paintEvent(QPaintEvent *) override;
    bool event(QEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;

  private:
    QPointer<QWidget> popup;
    QRect pendingPopupGeometry;
    QList<QPointer<QWidget>> watchedParents;
    QMetaObject::Connection screenConnection;
    void clearPopupWatch();
};
} // namespace rs
