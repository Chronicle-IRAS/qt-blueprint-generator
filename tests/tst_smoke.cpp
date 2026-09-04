#include <QtTest/QtTest>

#include "app/main_window.h"

class SmokeTest : public QObject
{
    Q_OBJECT

private slots:
    void mainWindowCanBeConstructedWithNonemptyTitle();
};

void SmokeTest::mainWindowCanBeConstructedWithNonemptyTitle()
{
    MainWindow window;

    QVERIFY(!window.windowTitle().isEmpty());
}

QTEST_MAIN(SmokeTest)

#include "tst_smoke.moc"
