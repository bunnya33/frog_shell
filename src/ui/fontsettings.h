#pragma once
#include <QWidget>
namespace rs {
class FontSettings final : public QWidget {
  public:
    explicit FontSettings(QWidget *parent = nullptr);
    void showZoomCategory(bool zoom);
  private:
    QWidget *fontPage = nullptr, *zoomPage = nullptr;
};
}
