#pragma once
#include <QApplication>
#include "../core/diagnostics.h"
namespace rs {
class DiagnosticApplication final : public QApplication {
  public:
    using QApplication::QApplication;
    bool notify(QObject *receiver,QEvent *event) override {
        DiagnosticDispatch dispatch(receiver,event);
        return QApplication::notify(receiver,event);
    }
};
}
