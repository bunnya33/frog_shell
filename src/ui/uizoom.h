#pragma once
#include <QtWidgets>
#include <functional>
namespace rs {
#ifdef Q_OS_MACOS
inline constexpr auto physicalControlModifier = Qt::MetaModifier;
#else
inline constexpr auto physicalControlModifier = Qt::ControlModifier;
#endif
// All arguments are canonical logical pixels, independent of screen DPR.
class UiZoomStyle;
class UiZoom final : public QObject {
    friend class AppearanceTests;
    Q_OBJECT
  public:
    explicit UiZoom(QObject *parent=nullptr);
    static UiZoom &instance();
    int level() const { return current; }
    qreal factor() const;
    void setLevel(int level);
    void reload();
    void bind(QObject *context,const QString &key,std::function<void()> apply);
    void applicationSheet(const QString &canonical);
    void ensureStyle();
    bool changing() const { return applying; }
  signals:
    void aboutToChange();
    void changed();
  protected:
    bool eventFilter(QObject *,QEvent *) override;
  private:
    int current=0;
    bool applying=false;
    QString canonicalSheet;
    QPointer<UiZoomStyle> installedStyle;
    QHash<QObject *,QMap<QString,std::function<void()>>> bindings;
    void apply();
};
int uiPx(qreal base);
qreal uiReal(qreal base);
qreal uiBase(qreal displayed);
QSize uiSize(int width,int height);
QFont uiFont(int basePx=13,QFont::Weight weight=QFont::Normal);
QString uiQss(const QString &canonical);
void setUiStyleSheet(QWidget *,const QString &canonical);
void bindUiFont(QWidget *,int basePx=13,QFont::Weight weight=QFont::Normal);
void uiMargins(QLayout *,int left,int top,int right,int bottom);
void uiSpacing(QLayout *,int base);
void uiFixedSize(QWidget *,int width,int height);
void uiFixedWidth(QWidget *,int base);
void uiFixedHeight(QWidget *,int base);
void uiMinimumWidth(QWidget *,int base);
void uiMinimumHeight(QWidget *,int base);
void uiMaximumWidth(QWidget *,int base);
void uiMaximumHeight(QWidget *,int base);
void uiIconSize(QAbstractButton *,int width,int height);
void uiIconSize(QAbstractItemView *,int width,int height);
void uiHandleWidth(QSplitter *,int base);
// Shared by the application style and StyledCombo's independent Fusion proxy.
class UiZoomStyle : public QProxyStyle {
  public:
    UiZoomStyle();
  private:
    mutable bool readingMetric=false;
  public:
    int pixelMetric(PixelMetric,const QStyleOption * =nullptr,const QWidget * =nullptr) const override;
    int styleHint(StyleHint,const QStyleOption * =nullptr,const QWidget * =nullptr,QStyleHintReturn * =nullptr) const override;
};
}
