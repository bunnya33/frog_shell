#include "../src/ui/styledcombobox.h"
#include "../src/ui/theme.h"
#include <QtTest>
namespace rs {
class PopupEvents : public QObject {
  public:
    int shows = 0, hides = 0;
    QRect shownGeometry;
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::Show) { ++shows; shownGeometry = static_cast<QWidget *>(object)->geometry(); }
        if (event->type() == QEvent::Hide) ++hides;
        return false;
    }
};
class ControlTests : public QObject {
    Q_OBJECT
    QTemporaryDir data;
    QString report;
    void settle() { QCoreApplication::processEvents(); QCoreApplication::sendPostedEvents(); QCoreApplication::processEvents(); }
    QRect anchor(QWidget &widget) { return QRect(widget.mapToGlobal(QPoint()), widget.size()); }
    void fill(StyledComboBox &combo) {
        for (int i = 0; i < 30; ++i)
            combo.addItem(QString("Network adapter %1 — production service").arg(i), i);
        combo.setCurrentIndex(24);
    }
    void saveComposite(QWidget &window, StyledComboBox &combo, const QString &name) {
        auto popup = combo.view()->window();
        const QRect bounds = anchor(window).united(popup->geometry());
        QImage image(bounds.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(colors().background);
        QPainter painter(&image);
        painter.drawPixmap(anchor(window).topLeft() - bounds.topLeft(), window.grab());
        painter.drawPixmap(popup->pos() - bounds.topLeft(), popup->grab());
        painter.end();
        QVERIFY(image.save(report + "/" + name + ".png"));
    }
  private slots:
    void initTestCase() {
        QCOMPARE(QGuiApplication::platformName(), QString("offscreen"));
        QVERIFY(data.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("RabbitShellControlTests");
        QCoreApplication::setApplicationName("RabbitShellControlTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, data.path());
        qputenv("RABBITSHELL_DATA_DIR", data.path().toUtf8());
        QApplication::setStyle("Fusion");
        for (auto path : {"C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/consola.ttf"})
            if (QFileInfo::exists(path)) QVERIFY(QFontDatabase::addApplicationFont(path) >= 0);
        QApplication::setFont(QFont("Microsoft YaHei UI", 10));
        report = qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR", data.path());
        QVERIFY(QDir().mkpath(report));
        applyTheme(false);
    }
    void popupGeometryAndSelection_data() {
        QTest::addColumn<bool>("bottom");
        QTest::newRow("middle-down") << false;
        QTest::newRow("bottom-up") << true;
    }
    void popupShowsOnlyOnce() {
        QWidget window; window.resize(500,400);
        StyledComboBox combo(&window); fill(combo); combo.setGeometry(20,20,200,34);
        window.show(); settle();
        auto popup = combo.view()->window(); PopupEvents events; popup->installEventFilter(&events);
        combo.showPopup(); settle();
        QVERIFY(popup->isVisible()); QCOMPARE(events.shows,1); QCOMPARE(events.hides,0);
        QCOMPARE(events.shownGeometry, popup->geometry());
        combo.hidePopup(); settle(); QCOMPARE(events.hides,1);
        popup->removeEventFilter(&events);
    }
    void popupWithComboAnimationEnabled() {
        const bool general=QApplication::isEffectEnabled(Qt::UI_General);
        const bool comboEffect=QApplication::isEffectEnabled(Qt::UI_AnimateCombo);
        QApplication::setEffectEnabled(Qt::UI_General,true);
        QApplication::setEffectEnabled(Qt::UI_AnimateCombo,true);
        QWidget window; window.resize(500,400);
        StyledComboBox combo(&window); fill(combo); combo.setGeometry(20,20,200,34);
        window.show(); settle();
        auto popup=combo.view()->window(); PopupEvents events; popup->installEventFilter(&events);
        combo.showPopup(); QTest::qWait(200);
        const bool visible=popup->isVisible();
        const bool restored=QApplication::isEffectEnabled(Qt::UI_AnimateCombo);
        const int shows=events.shows, hides=events.hides;
        combo.hidePopup(); popup->removeEventFilter(&events);
        QApplication::setEffectEnabled(Qt::UI_AnimateCombo,comboEffect);
        QApplication::setEffectEnabled(Qt::UI_General,general);
        QVERIFY(visible); QVERIFY(restored); QCOMPARE(shows,1); QCOMPARE(hides,0);
    }
    void popupGeometryAndSelection() {
        QFETCH(bool, bottom);
        QWidget window;
        const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
        window.setGeometry(available.adjusted(20, 20, -20, -20));
        StyledComboBox combo(&window);
        fill(combo);
        combo.setGeometry(window.width() - 190, bottom ? window.height() - 54 : 100, 170, 34);
        window.show(); window.activateWindow(); combo.setFocus(); settle();
        QSignalSpy changed(&combo, &QComboBox::currentIndexChanged), activated(&combo, &QComboBox::activated);
        combo.showPopup(); settle();
        auto popup = combo.view()->window();
        QVERIFY(popup->isVisible());
        QVERIFY(available.contains(popup->geometry()));
        QVERIFY(!popup->geometry().intersects(anchor(combo)));
        if (bottom) QCOMPARE(popup->geometry().bottom() + 5, anchor(combo).top());
        else QCOMPARE(popup->geometry().top(), anchor(combo).bottom() + 5);
        QVERIFY(combo.view()->visualRect(combo.view()->currentIndex()).intersects(combo.view()->viewport()->rect()));
        QTest::keyClick(combo.view(), Qt::Key_Down);
        QTest::keyClick(combo.view(), Qt::Key_Escape); settle();
        QVERIFY(!popup->isVisible());
        QCOMPARE(combo.currentIndex(), 24);
        QCOMPARE(changed.size(), 0);
        QCOMPARE(activated.size(), 0);
        combo.showPopup(); settle();
        QTest::keyClick(combo.view(), Qt::Key_Down);
        QTest::keyClick(combo.view(), Qt::Key_Return); settle();
        QCOMPARE(combo.currentIndex(), 25);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(activated.size(), 1);
        QVERIFY(!popup->isVisible());
        combo.showPopup(); settle();
        window.move(window.pos() + QPoint(1, 1)); settle();
        QVERIFY(!popup->isVisible());
        QCOMPARE(changed.size(), 1);
    }
    void limitedSpaceAndMouseSelection() {
        QWidget window;
        const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
        window.setGeometry(available.adjusted(10, 10, -10, -10));
        StyledComboBox combo(&window);
        fill(combo);
        combo.setMaxVisibleItems(30);
        combo.setGeometry(20, window.height() / 2, 160, 34);
        window.show(); settle();
        QSignalSpy changed(&combo, &QComboBox::currentIndexChanged), activated(&combo, &QComboBox::activated);
        combo.showPopup(); settle();
        auto popup = combo.view()->window();
        QVERIFY(available.contains(popup->geometry()));
        QVERIFY(!popup->geometry().intersects(anchor(combo)));
        QVERIFY(combo.view()->verticalScrollBar()->maximum() > 0);
        const auto target = combo.model()->index(23, 0);
        combo.view()->scrollTo(target); settle();
        QTest::mouseClick(combo.view()->viewport(), Qt::LeftButton, {}, combo.view()->visualRect(target).center());
        QCOMPARE(combo.currentIndex(), 23);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(activated.size(), 1);
    }
    void themeEvidence() {
        for (bool light : {false, true}) {
            applyTheme(light);
            QWidget window;
            window.resize(520, 440); window.move(20, 20);
            StyledComboBox combo(&window), disabled(&window);
            fill(combo);
            combo.setGeometry(24, 28, 270, 34);
            disabled.addItems({"已禁用", "其他"}); disabled.setEnabled(false);
            disabled.setGeometry(24, 82, 270, 34);
            window.show(); window.activateWindow(); combo.setFocus(); settle();
            const QString prefix = light ? "combo-light" : "combo-dark";
            QVERIFY(window.grab().save(report + "/" + prefix + "-closed.png"));
            combo.showPopup(); settle();
            saveComposite(window, combo, prefix + "-open");
            combo.hidePopup();
        }
    }
};
} // namespace rs
int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    rs::ControlTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "test_controls.moc"
