#pragma once
#include <QtWidgets>
namespace rs {
// Keeps QMessageBox's buttons, roles, results, signals and modal lifecycle.
// Only its visual layout is shared across application prompts.
class AppMessageBox final:public QMessageBox {
  public:
    explicit AppMessageBox(QWidget *parent=nullptr);
    AppMessageBox(Icon icon,const QString &title,const QString &text,StandardButtons buttons=NoButton,QWidget *parent=nullptr);
    static StandardButton information(QWidget *,const QString &,const QString &,StandardButtons=Ok,StandardButton=NoButton);
    static StandardButton warning(QWidget *,const QString &,const QString &,StandardButtons=Ok,StandardButton=NoButton);
    static StandardButton critical(QWidget *,const QString &,const QString &,StandardButtons=Ok,StandardButton=NoButton);
    static StandardButton question(QWidget *,const QString &,const QString &,StandardButtons=Yes|No,StandardButton=NoButton);
    void setText(const QString &value){QMessageBox::setText(value);queueArrange();}
    void setInformativeText(const QString &value){QMessageBox::setInformativeText(value);queueArrange();}
    void setDetailedText(const QString &value){QMessageBox::setDetailedText(value);queueArrange();}
    void setCheckBox(QCheckBox *value){QMessageBox::setCheckBox(value);queueArrange();}
  protected:
    void showEvent(QShowEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void changeEvent(QEvent *) override;
    bool event(QEvent *) override;
  private:
    bool arranging=false,queued=false;
    QPointer<QLayout> visualLayout;
    QScrollArea *scroll=nullptr;
    QWidget *body=nullptr,*footer=nullptr,*symbol=nullptr;
    QLabel *message=nullptr,*informationText=nullptr;
    void arrange();
    void fit();
    void queueArrange();
    void localizeButtons();
    static StandardButton showMessage(QWidget *,Icon,const QString &,const QString &,StandardButtons,StandardButton);
};
}
