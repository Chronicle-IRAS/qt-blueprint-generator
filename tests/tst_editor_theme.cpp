#include <QtTest>
#include <QApplication>
#include <QFormLayout>
#include <QHeaderView>
#include <QListWidget>
#include <QScrollArea>
#include <QSplitter>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTranslator>
#include <QDockWidget>
#include <QGraphicsView>
#include <QToolBar>
#include <QAction>
#include <QFontInfo>
#include <QLineEdit>
#include <cmath>
#include "ui/theme.h"
#include "app/main_window.h"
#include "app/ai_settings_dialog.h"
#include "app/candidate_review_dialog.h"
#include "editor/node_properties_editor.h"

class EditorThemeTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { EditorTheme::apply(*qApp); }
    void reapplicationIsStable() {
        const auto palette = qApp->palette();
        const auto font = qApp->font();
        const auto style = qApp->styleSheet();
        EditorTheme::apply(*qApp);
        QCOMPARE(qApp->palette(), palette);
        QCOMPARE(qApp->font(), font);
        QCOMPARE(qApp->styleSheet(), style);
    }
    void lightPalette() {
        QCOMPARE(qApp->palette().color(QPalette::Window), QColor("#f1f5f9"));
        QCOMPARE(qApp->palette().color(QPalette::Highlight), QColor("#1d4ed8"));
        if (QGuiApplication::platformName() == "windows")
            QVERIFY(QFontInfo(EditorTheme::codeFont()).fixedPitch());
    }
    void textContrast() {
        auto luminance = [](QColor color) {
            auto channel = [](double value) { return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4); };
            return .2126 * channel(color.redF()) + .7152 * channel(color.greenF()) + .0722 * channel(color.blueF());
        };
        const auto &c = EditorTheme::colors();
        for (auto pair : {qMakePair(c.text,c.surface), qMakePair(c.textMuted,c.window),
                          qMakePair(c.onAccent,c.accent), qMakePair(c.disabledText,c.disabledSurface)}) {
            const double first = luminance(pair.first), second = luminance(pair.second);
            QVERIFY((qMax(first,second)+.05)/(qMin(first,second)+.05) >= 4.5);
        }
    }
    void inspectorBoundsTextColumns() {
        NodePropertiesEditor editor("inspector");
        const auto tables = editor.findChildren<QTableWidget *>("portItemsTable");
        QCOMPARE(tables.size(), 2);
        for (auto *table : tables) {
            QCOMPARE(table->horizontalHeader()->sectionResizeMode(0), QHeaderView::Stretch);
            QCOMPARE(table->horizontalHeader()->sectionResizeMode(1), QHeaderView::Stretch);
        }
        const auto addButtons = editor.findChildren<QPushButton *>("addPortButton");
        QCOMPARE(addButtons.size(), 2);
        for (auto *button : addButtons) button->click();
        editor.resize(340, 760);
        editor.show();
        QTest::qWait(20);
        for (auto *table : tables) {
            QCOMPARE(table->rowCount(), 1);
            auto *button = qobject_cast<QPushButton *>(table->cellWidget(0, 3));
            QVERIFY(button);
            QVERIFY(table->rowHeight(0) >= button->sizeHint().height());
        }
    }
    void buildFormScrollsIndependently() {
        MainWindow window;
        auto *scroll = window.findChild<QScrollArea *>("buildFormScrollArea");
        QVERIFY(scroll);
        QVERIFY(scroll->widgetResizable());
        QVERIFY(scroll->maximumHeight() <= 112);
        QCOMPARE(window.findChild<QPlainTextEdit *>("buildLog")->font().family(), EditorTheme::codeFont().family());
        window.resize(1280, 800);
        window.show();
        QTest::qWait(20);
        QVERIFY(window.findChild<QDockWidget *>("buildExportDock")->height() <= 280);
        // Native Windows may constrain the requested size to the available screen.
        QVERIFY(window.centralWidget()->height() >= window.height() - 380);
        auto *action = window.findChild<QAction *>("generateSelectedNodeAction");
        QVERIFY(action);
        QCOMPARE(window.findChild<QToolBar *>("blueprintToolbar")->widgetForAction(action)->property("role").toString(), QString("primary"));
    }
    void settingsWrapFields_data() {
        QTest::addColumn<bool>("chinese");
        QTest::newRow("English") << false;
        QTest::newRow("Chinese") << true;
    }
    void settingsSaveUsesPrimaryColors() {
        AiSettingsDialog dialog;
        dialog.show();
        QApplication::processEvents();
        auto *save = dialog.findChild<QPushButton *>("aiSettingsSaveButton");
        QVERIFY(save);
        QCOMPARE(save->palette().color(QPalette::Button), EditorTheme::colors().accent);
        QCOMPARE(save->palette().color(QPalette::ButtonText), EditorTheme::colors().onAccent);
    }
    void settingsWrapFields() {
        QFETCH(bool, chinese);
        QTranslator translator;
        if (chinese) {
            QVERIFY(translator.load(":/i18n/BlueprintEditor_zh_CN.qm"));
            qApp->installTranslator(&translator);
        }
        AiSettingsDialog dialog;
        QCOMPARE(dialog.findChild<QFormLayout *>()->rowWrapPolicy(), QFormLayout::WrapLongRows);
        QVERIFY(dialog.findChild<QLabel *>("aiApiKeyStatusLabel")->wordWrap());
        QVERIFY(dialog.findChild<QLabel *>("aiConnectionStatusLabel")->wordWrap());
        auto *form = dialog.findChild<QFormLayout *>();
        auto *label = qobject_cast<QLabel *>(form->labelForField(dialog.findChild<QLineEdit *>("aiEndpointEdit")));
        QVERIFY(label);
        QVERIFY(!label->text().isEmpty());
        QCOMPARE(label->text(), QCoreApplication::translate("AiSettingsDialog", "Chat Completions Endpoint"));
        QCOMPARE(form->rowCount(), 6);
        for (int row = 0; row < form->rowCount(); ++row) {
            auto *item = form->itemAt(row, QFormLayout::LabelRole);
            QVERIFY(item);
            auto *rowLabel = qobject_cast<QLabel *>(item->widget());
            QVERIFY(rowLabel);
            QVERIFY(!rowLabel->text().isEmpty());
        }
    }
    void candidateKeepsEditorsVisible_data() {
        QTest::addColumn<bool>("chinese");
        QTest::newRow("English") << false;
        QTest::newRow("Chinese") << true;
    }
    void candidateKeepsEditorsVisible() {
        QFETCH(bool, chinese);
        QTranslator translator;
        if (chinese) {
            QVERIFY(translator.load(":/i18n/BlueprintEditor_zh_CN.qm"));
            qApp->installTranslator(&translator);
        }
        CandidateReviewDialog dialog(BlueprintDocument{}, {}, {}, GenerationResult{});
        auto *splitter = dialog.findChild<QSplitter *>();
        QVERIFY(!splitter->childrenCollapsible());
        QVERIFY(dialog.findChild<QListWidget *>("candidateFiles")->maximumHeight() <= 112);
        dialog.resize(960, 640);
        dialog.show();
        QTest::qWait(20);
        QCOMPARE(dialog.width(), 960);
        QVERIFY(dialog.findChild<QPushButton *>("cancelRemainingButton")->y() >
                dialog.findChild<QPushButton *>("acceptCandidateButton")->y());
        for (auto *button : dialog.findChildren<QPushButton *>())
            QVERIFY(button->width() >= button->sizeHint().width());
    }
};
QTEST_MAIN(EditorThemeTest)
#include "tst_editor_theme.moc"
