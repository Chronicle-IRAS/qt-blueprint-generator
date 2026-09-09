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
        auto *viewMenu = window.findChild<QMenu *>(QStringLiteral("viewMenu"));
        auto *englishAction = window.findChild<QAction *>(QStringLiteral("languageEnglishAction"));
        auto *chineseAction = window.findChild<QAction *>(QStringLiteral("languageChineseAction"));
        auto *propertiesAction = window.findChild<QAction *>(QStringLiteral("propertiesDockAction"));
        auto *buildDockAction = window.findChild<QAction *>(QStringLiteral("buildExportDockAction"));
        auto *resetLayoutAction = window.findChild<QAction *>(QStringLiteral("resetLayoutAction"));
        auto *toolbarBuildAction = window.findChild<QAction *>(QStringLiteral("toolbarBuildAction"));
        auto *toolbarExportAction = window.findChild<QAction *>(QStringLiteral("toolbarExportAction"));
        auto *propertiesDock = window.findChild<QDockWidget *>(QStringLiteral("propertiesDock"));
        auto *buildButton = window.findChild<QPushButton *>(QStringLiteral("buildProjectButton"));
        QVERIFY(languageMenu && viewMenu && englishAction && chineseAction && propertiesAction
                && buildDockAction && resetLayoutAction && toolbarBuildAction
                && toolbarExportAction && propertiesDock && buildButton);

        QCOMPARE(window.currentLanguage(), QStringLiteral("en"));
        QVERIFY(!QSettings().contains(QStringLiteral("ui/language")));
        QCOMPARE(window.windowTitle(), QStringLiteral("Blueprint Editor"));
        QCOMPARE(viewMenu->title(), QStringLiteral("View"));
        QCOMPARE(propertiesAction->text(), QStringLiteral("Properties"));
        QCOMPARE(buildDockAction->text(), QStringLiteral("Build and export"));
        QCOMPARE(resetLayoutAction->text(), QStringLiteral("Reset Layout"));
        QCOMPARE(toolbarBuildAction->text(), QStringLiteral("Build"));
        QCOMPARE(toolbarExportAction->text(), QStringLiteral("Export"));
        QVERIFY(englishAction->isChecked());

        chineseAction->trigger();
        QCOMPARE(window.currentLanguage(), QStringLiteral("zh_CN"));
        QCOMPARE(window.windowTitle(), QStringLiteral("蓝图编辑器"));
        QCOMPARE(languageMenu->title(), QStringLiteral("语言"));
        QCOMPARE(viewMenu->title(), QStringLiteral("视图"));
        QCOMPARE(propertiesAction->text(), QStringLiteral("属性"));
        QCOMPARE(buildDockAction->text(), QStringLiteral("构建与导出"));
        QCOMPARE(resetLayoutAction->text(), QStringLiteral("重置布局"));
        QCOMPARE(toolbarBuildAction->text(), QStringLiteral("构建"));
        QCOMPARE(toolbarExportAction->text(), QStringLiteral("导出"));
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
    QCOMPARE(QSettings().value(QStringLiteral("ui/language")).toString(),
             QStringLiteral("en"));
}

QTEST_MAIN(LanguageSwitchTest)

#include "tst_language_switch.moc"
