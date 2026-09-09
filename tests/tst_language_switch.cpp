#include <QtTest/QtTest>

#include <QAction>
#include <QDockWidget>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>

#include "app/main_window.h"

class LanguageSwitchTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void switchesBetweenEnglishAndChineseAndPersistsChoice();

private:
    QTemporaryDir m_settingsDirectory;
};

void LanguageSwitchTest::initTestCase()
{
    QVERIFY(m_settingsDirectory.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("QtBlueprintGeneratorTests"));
    QCoreApplication::setApplicationName(QStringLiteral("LanguageSwitchTest"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       m_settingsDirectory.path());
    QSettings().clear();
}

void LanguageSwitchTest::cleanupTestCase()
{
    QSettings().clear();
}

void LanguageSwitchTest::switchesBetweenEnglishAndChineseAndPersistsChoice()
{
    {
        MainWindow window;
        auto *languageMenu = window.findChild<QMenu *>(QStringLiteral("languageMenu"));
        auto *englishAction = window.findChild<QAction *>(QStringLiteral("languageEnglishAction"));
        auto *chineseAction = window.findChild<QAction *>(QStringLiteral("languageChineseAction"));
        auto *propertiesDock = window.findChild<QDockWidget *>(QStringLiteral("propertiesDock"));
        auto *buildButton = window.findChild<QPushButton *>(QStringLiteral("buildProjectButton"));
        QVERIFY(languageMenu && englishAction && chineseAction && propertiesDock && buildButton);

        QCOMPARE(window.currentLanguage(), QStringLiteral("en"));
        QCOMPARE(window.windowTitle(), QStringLiteral("Blueprint Editor"));
        QVERIFY(englishAction->isChecked());

        chineseAction->trigger();
        QCOMPARE(window.currentLanguage(), QStringLiteral("zh_CN"));
        QCOMPARE(window.windowTitle(), QStringLiteral("蓝图编辑器"));
        QCOMPARE(languageMenu->title(), QStringLiteral("语言"));
        QCOMPARE(propertiesDock->windowTitle(), QStringLiteral("属性"));
        QCOMPARE(buildButton->text(), QStringLiteral("构建"));
        QVERIFY(chineseAction->isChecked());
        QVERIFY(window.addNodeOfType(NodeType::LogicModule));
        QCOMPARE(window.document().nodes.constFirst().name, QStringLiteral("逻辑模块"));

        englishAction->trigger();
        QCOMPARE(window.currentLanguage(), QStringLiteral("en"));
        QCOMPARE(window.windowTitle(), QStringLiteral("Blueprint Editor"));
        QCOMPARE(languageMenu->title(), QStringLiteral("Language"));
        QCOMPARE(propertiesDock->windowTitle(), QStringLiteral("Properties"));
        QCOMPARE(buildButton->text(), QStringLiteral("Build"));
        QVERIFY(englishAction->isChecked());
        QCOMPARE(window.document().nodes.constFirst().name, QStringLiteral("逻辑模块"));

        chineseAction->trigger();
        QCOMPARE(QSettings().value(QStringLiteral("ui/language")).toString(),
                 QStringLiteral("zh_CN"));
    }

    MainWindow restoredWindow;
    QCOMPARE(restoredWindow.currentLanguage(), QStringLiteral("zh_CN"));
    QCOMPARE(restoredWindow.windowTitle(), QStringLiteral("蓝图编辑器"));
    QVERIFY(restoredWindow.setLanguage(QStringLiteral("en")));
    QVERIFY(!restoredWindow.setLanguage(QStringLiteral("fr")));
    QCOMPARE(restoredWindow.currentLanguage(), QStringLiteral("en"));
}

QTEST_MAIN(LanguageSwitchTest)

#include "tst_language_switch.moc"
