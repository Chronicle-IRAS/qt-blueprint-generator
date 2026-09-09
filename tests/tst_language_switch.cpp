#include <QtTest/QtTest>

#include <QAction>
#include <QDialog>
#include <QDockWidget>
#include <QGraphicsView>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

#include "app/main_window.h"
#include "editor/blueprint_scene.h"
#include "editor/node_item.h"

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
        QVERIFY(!QSettings().contains(QStringLiteral("ui/language")));
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
        window.show();
        QApplication::processEvents();
        bool dialogOpened = false;
        QString dialogTitle;
        QString saveText;
        QString cancelText;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) {
                return;
            }
            dialogOpened = true;
            dialogTitle = dialog->windowTitle();
            auto *save = dialog->findChild<QPushButton *>(QStringLiteral("saveNodeEditButton"));
            auto *cancel = dialog->findChild<QPushButton *>(QStringLiteral("cancelNodeEditButton"));
            if (save) {
                saveText = save->text();
            }
            if (cancel) {
                cancelText = cancel->text();
                QTest::mouseClick(cancel, Qt::LeftButton);
            } else {
                dialog->reject();
            }
        });
        const QString nodeId = window.document().nodes.constFirst().id;
        const QPoint nodeCenter = window.graphicsView()->mapFromScene(
            window.scene()->nodeItem(nodeId)->sceneBoundingRect().center());
        QTest::mouseDClick(window.graphicsView()->viewport(), Qt::LeftButton,
                           Qt::NoModifier, nodeCenter);
        QApplication::processEvents();
        QVERIFY(dialogOpened);
        QCOMPARE(dialogTitle, QStringLiteral("编辑节点"));
        QCOMPARE(saveText, QStringLiteral("保存"));
        QCOMPARE(cancelText, QStringLiteral("取消"));

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
