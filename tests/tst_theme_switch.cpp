#include <QtTest/QtTest>

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QPalette>
#include <QSettings>
#include <QTemporaryDir>

#include "app/main_window.h"
#include "editor/blueprint_scene.h"
#include "editor/node_item.h"
#include "ui/theme.h"

namespace {

// What a user observes after a theme switch: the application palette roles, the stylesheet
// tokens derived from the theme colours, and a stylesheet that has no unresolved placeholder.
void verifyApplicationUsesTheme(const EditorTheme::Colors &colors)
{
    QCOMPARE(qApp->palette().color(QPalette::Window), colors.window);
    QCOMPARE(qApp->palette().color(QPalette::Base), colors.surface);
    QCOMPARE(qApp->palette().color(QPalette::Text), colors.text);
    QCOMPARE(qApp->palette().color(QPalette::Highlight), colors.accent);
    const QString styleSheet = qApp->styleSheet();
    QVERIFY(styleSheet.contains(colors.surface.name()));
    QVERIFY(styleSheet.contains(colors.accent.name()));
    for (int index = 1; index <= 11; ++index) {
        QVERIFY(!styleSheet.contains(QStringLiteral("%") + QString::number(index)));
    }
}

} // namespace

class ThemeSwitchTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();
    void themeMenuSwitchesAtRuntimeAndPersistsTheChoice();
    void repeatedThemeSwitchesAreStable();
    void themeMenuFollowsTheLanguageSwitch();
    void unknownStoredThemeFallsBackToLight();
    void themeSwitchKeepsTheDocumentAndUndoStackUntouched();

private:
    QTemporaryDir m_settingsDirectory;
};

void ThemeSwitchTest::initTestCase()
{
    QVERIFY(m_settingsDirectory.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("QtBlueprintGeneratorTests"));
    QCoreApplication::setApplicationName(QStringLiteral("ThemeSwitchTest"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       m_settingsDirectory.path());
    QSettings().clear();
}

void ThemeSwitchTest::cleanupTestCase()
{
    QSettings().clear();
}

// Even a failed assertion must not leave a switched theme behind for the next test.
void ThemeSwitchTest::cleanup()
{
    EditorTheme::setTheme(*qApp, EditorTheme::Theme::Light);
}

void ThemeSwitchTest::themeMenuSwitchesAtRuntimeAndPersistsTheChoice()
{
    {
        MainWindow window;
        auto *themeMenu = window.findChild<QMenu *>(QStringLiteral("themeMenu"));
        auto *lightAction = window.findChild<QAction *>(QStringLiteral("themeLightAction"));
        auto *darkAction = window.findChild<QAction *>(QStringLiteral("themeDarkAction"));
        QVERIFY(themeMenu && lightAction && darkAction);
        QVERIFY(!QSettings().contains(QStringLiteral("ui/theme")));

        QVERIFY(lightAction->isChecked());
        QVERIFY(!darkAction->isChecked());
        verifyApplicationUsesTheme(EditorTheme::colors(EditorTheme::Theme::Light));

        darkAction->trigger();
        QVERIFY(darkAction->isChecked());
        QVERIFY(!lightAction->isChecked());
        verifyApplicationUsesTheme(EditorTheme::colors(EditorTheme::Theme::Dark));
        QCOMPARE(QSettings().value(QStringLiteral("ui/theme")).toString(),
                 QStringLiteral("dark"));

        // Opening a window while dark is stored picks the choice up and leaves it alone.
        const QString stored = QSettings().value(QStringLiteral("ui/theme")).toString();
        {
            MainWindow restored;
            auto *restoredLight = restored.findChild<QAction *>(
                QStringLiteral("themeLightAction"));
            auto *restoredDark = restored.findChild<QAction *>(
                QStringLiteral("themeDarkAction"));
            QVERIFY(restoredLight && restoredDark);
            QVERIFY(restoredDark->isChecked());
            QVERIFY(!restoredLight->isChecked());
            verifyApplicationUsesTheme(EditorTheme::colors(EditorTheme::Theme::Dark));
            QCOMPARE(QSettings().value(QStringLiteral("ui/theme")).toString(), stored);

            restoredLight->trigger();
            QVERIFY(restoredLight->isChecked());
            QVERIFY(!restoredDark->isChecked());
            verifyApplicationUsesTheme(EditorTheme::colors(EditorTheme::Theme::Light));
            QCOMPARE(QSettings().value(QStringLiteral("ui/theme")).toString(),
                     QStringLiteral("light"));
        }
    }
}

void ThemeSwitchTest::repeatedThemeSwitchesAreStable()
{
    EditorTheme::setTheme(*qApp, EditorTheme::Theme::Light);
    const QPalette lightPalette = qApp->palette();
    const QString lightStyleSheet = qApp->styleSheet();
    EditorTheme::setTheme(*qApp, EditorTheme::Theme::Dark);
    const QPalette darkPalette = qApp->palette();
    const QString darkStyleSheet = qApp->styleSheet();
    // Guard the recorded snapshots themselves, otherwise equal values would prove nothing.
    QVERIFY(lightStyleSheet != darkStyleSheet);
    QVERIFY(lightPalette.color(QPalette::Window) != darkPalette.color(QPalette::Window));

    for (int round = 0; round < 2; ++round) {
        EditorTheme::setTheme(*qApp, EditorTheme::Theme::Light);
        QCOMPARE(qApp->palette(), lightPalette);
        QCOMPARE(qApp->styleSheet(), lightStyleSheet);
        EditorTheme::setTheme(*qApp, EditorTheme::Theme::Dark);
        QCOMPARE(qApp->palette(), darkPalette);
        QCOMPARE(qApp->styleSheet(), darkStyleSheet);
    }

    EditorTheme::setTheme(*qApp, EditorTheme::Theme::Light);
    QCOMPARE(qApp->palette(), lightPalette);
    QCOMPARE(qApp->styleSheet(), lightStyleSheet);
}

void ThemeSwitchTest::themeMenuFollowsTheLanguageSwitch()
{
    MainWindow window;
    auto *themeMenu = window.findChild<QMenu *>(QStringLiteral("themeMenu"));
    auto *lightAction = window.findChild<QAction *>(QStringLiteral("themeLightAction"));
    auto *darkAction = window.findChild<QAction *>(QStringLiteral("themeDarkAction"));
    QVERIFY(themeMenu && lightAction && darkAction);
    QVERIFY(window.setLanguage(QStringLiteral("en")));
    darkAction->trigger();
    QCOMPARE(themeMenu->title(), QStringLiteral("Theme"));
    QCOMPARE(lightAction->text(), QStringLiteral("Light"));
    QCOMPARE(darkAction->text(), QStringLiteral("Dark"));

    QVERIFY(window.setLanguage(QStringLiteral("zh_CN")));
    QCOMPARE(themeMenu->title(), QStringLiteral("主题"));
    QCOMPARE(lightAction->text(), QStringLiteral("亮色"));
    QCOMPARE(darkAction->text(), QStringLiteral("暗色"));
    // Retranslating must not touch the selection, nor drop the applied theme.
    QVERIFY(darkAction->isChecked());
    QVERIFY(!lightAction->isChecked());
    verifyApplicationUsesTheme(EditorTheme::colors(EditorTheme::Theme::Dark));

    QVERIFY(window.setLanguage(QStringLiteral("en")));
    QCOMPARE(themeMenu->title(), QStringLiteral("Theme"));
    QCOMPARE(lightAction->text(), QStringLiteral("Light"));
    QCOMPARE(darkAction->text(), QStringLiteral("Dark"));
    QVERIFY(darkAction->isChecked());
    QVERIFY(!lightAction->isChecked());
    verifyApplicationUsesTheme(EditorTheme::colors(EditorTheme::Theme::Dark));

    lightAction->trigger();
    verifyApplicationUsesTheme(EditorTheme::colors(EditorTheme::Theme::Light));
    QCOMPARE(QSettings().value(QStringLiteral("ui/theme")).toString(), QStringLiteral("light"));
}

void ThemeSwitchTest::unknownStoredThemeFallsBackToLight()
{
    QSettings().setValue(QStringLiteral("ui/theme"), QStringLiteral("solarized"));
    MainWindow window;
    auto *lightAction = window.findChild<QAction *>(QStringLiteral("themeLightAction"));
    auto *darkAction = window.findChild<QAction *>(QStringLiteral("themeDarkAction"));
    QVERIFY(lightAction && darkAction);
    QVERIFY(lightAction->isChecked());
    QVERIFY(!darkAction->isChecked());
    verifyApplicationUsesTheme(EditorTheme::colors(EditorTheme::Theme::Light));
    // Startup falls back without rewriting what the user has stored.
    QCOMPARE(QSettings().value(QStringLiteral("ui/theme")).toString(),
             QStringLiteral("solarized"));
    QSettings().remove(QStringLiteral("ui/theme"));
}

void ThemeSwitchTest::themeSwitchKeepsTheDocumentAndUndoStackUntouched()
{
    MainWindow window;
    QVERIFY(window.addNodeOfType(NodeType::Start));
    QVERIFY(window.addNodeOfType(NodeType::End));
    const QString source = window.document().nodes.at(0).id;
    const QString target = window.document().nodes.at(1).id;
    QVERIFY(window.scene()->connectNodes(source, target, QStringLiteral("next")));
    const BlueprintDocument before = window.document();
    const int undoCount = window.scene()->undoStack()->count();
    const QString undoText = window.scene()->undoStack()->undoText();
    QVERIFY(undoCount > 0);

    auto *darkAction = window.findChild<QAction *>(QStringLiteral("themeDarkAction"));
    auto *lightAction = window.findChild<QAction *>(QStringLiteral("themeLightAction"));
    QVERIFY(darkAction && lightAction);
    darkAction->trigger();

    QCOMPARE(window.document(), before);
    QCOMPARE(window.scene()->undoStack()->count(), undoCount);
    QCOMPARE(window.scene()->undoStack()->undoText(), undoText);
    QVERIFY(window.scene()->nodeItem(source) != nullptr);
    QVERIFY(window.scene()->nodeItem(target) != nullptr);
    QCOMPARE(window.document().edges.size(), 1);

    lightAction->trigger();
    QCOMPARE(window.document(), before);
    QCOMPARE(window.scene()->undoStack()->count(), undoCount);
}

QTEST_MAIN(ThemeSwitchTest)

#include "tst_theme_switch.moc"
