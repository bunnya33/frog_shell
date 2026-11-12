#pragma once
#include <QtWidgets>

namespace rs {
// Keep system decorations and delegate platform-specific colors to one adapter.
void installWindowStyle();
void refreshWindowStyles();
void keepWindowOnScreen(QWidget *window);
QRect fitWindowToScreens(const QRect &requested, const QList<QRect> &screens);
QIcon applicationIcon();
void initializeBrandingResources();

class ElidedLabel : public QLabel {
  public:
    explicit ElidedLabel(const QString &text = {}, QWidget *parent = nullptr);
    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;
    void setText(const QString &text);

  protected:
    void paintEvent(QPaintEvent *) override;
};

class ThemeChoice : public QRadioButton {
  public:
    ThemeChoice(const QString &name, bool light, QWidget *parent = nullptr);
    QSize sizeHint() const override;

  protected:
    bool hitButton(const QPoint &position) const override;
    void paintEvent(QPaintEvent *) override;

  private:
    bool previewLight;
};
} // namespace rs
