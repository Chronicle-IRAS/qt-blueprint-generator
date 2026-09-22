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
#include <QGraphicsView>
#include <QImage>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QVBoxLayout>
#include <cmath>
#include "ui/theme.h"
#include "app/main_window.h"
#include "app/ai_settings_dialog.h"
#include "app/candidate_review_dialog.h"
#include "editor/blueprint_scene.h"
#include "editor/edge_item.h"
#include "editor/node_item.h"
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
    void darkPalette() {
        // Identity values of the dark set, read straight from the theme so the slot stays
        // independent of whatever theme is applied (and of any stored setting).
        const auto &dark = EditorTheme::colors(EditorTheme::Theme::Dark);
        QCOMPARE(dark.window, QColor("#0f172a"));
        QCOMPARE(dark.canvas, QColor("#0b1220"));
        QCOMPARE(dark.surface, QColor("#1e293b"));
        QCOMPARE(dark.accent, QColor("#2563eb"));
    }
    void textContrast_data() {
        QTest::addColumn<int>("theme");
        QTest::newRow("Light") << static_cast<int>(EditorTheme::Theme::Light);
        QTest::newRow("Dark") << static_cast<int>(EditorTheme::Theme::Dark);
    }
    void textContrast() {
        QFETCH(int, theme);
        auto luminance = [](QColor color) {
            auto channel = [](double value) { return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4); };
            return .2126 * channel(color.redF()) + .7152 * channel(color.greenF()) + .0722 * channel(color.blueF());
        };
        const auto &c = EditorTheme::colors(static_cast<EditorTheme::Theme>(theme));
        auto contrast = [&luminance](const QColor &first, const QColor &second) {
            const double a = luminance(first), b = luminance(second);
            return (qMax(a, b) + .05) / (qMin(a, b) + .05);
        };
        // WCAG AA for text on the surface it is painted on.
        for (auto pair : {qMakePair(c.text, c.surface), qMakePair(c.textMuted, c.window),
                          qMakePair(c.onAccent, c.accent), qMakePair(c.disabledText, c.disabledSurface),
                          qMakePair(c.nodeHeaderText, c.nodeHeader), qMakePair(c.text, c.diffAdded),
                          qMakePair(c.text, c.diffRemoved)}) {
            QVERIFY(contrast(pair.first, pair.second) >= 4.5);
        }
        // Outlines, ports and selections only have to be told apart from the shape they belong to.
        for (auto pair : {qMakePair(c.nodeBorder, c.nodeBody), qMakePair(c.edge, c.canvas),
                          qMakePair(c.selection, c.nodeBody), qMakePair(c.selection, c.canvas),
                          qMakePair(c.portInput, c.nodeBody), qMakePair(c.portOutput, c.nodeBody),
                          qMakePair(c.portCompatible, c.nodeBody), qMakePair(c.textMuted, c.surfaceAlt)}) {
            QVERIFY(contrast(pair.first, pair.second) >= 3.0);
        }
        // The grid lines and the node body stay deliberately soft against the canvas, and the
        // borders stay soft against the window and the accent: the dark theme keeps them as
        // subtle as the light one, so those pairs are excluded on purpose.
    }
    void customItemsFollowTheActiveTheme() {
        BlueprintDocument document;
        BlueprintScene scene(&document);
        QGraphicsView view;
        view.setScene(&scene);
        BlueprintNode first;
        first.id = QStringLiteral("first");
        first.type = NodeType::LogicModule;
        first.name = QStringLiteral("First");
        BlueprintNode second = first;
        second.id = QStringLiteral("second");
        second.name = QStringLiteral("Second");
        QVERIFY(scene.addNode(first, QPointF(0, 0)));
        QVERIFY(scene.addNode(second, QPointF(320, 0)));
        QVERIFY(scene.connectNodes(QStringLiteral("first"), QStringLiteral("second")));
        QCOMPARE(document.edges.size(), 1);
        QVERIFY(document.edges.constFirst().label.isEmpty());
        NodeItem *firstItem = scene.nodeItem(QStringLiteral("first"));
        EdgeItem *edge = scene.edgeItem(document.edges.constFirst().id);
        QVERIFY(firstItem && edge);

        // One scene unit maps to one image pixel, so a scene point can be sampled directly.
        const QRectF region(0, 0, 700, 400);
        auto render = [&scene, region](EditorTheme::Theme theme) {
            EditorTheme::setTheme(*qApp, theme);
            QImage image(700, 400, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            // Antialiasing off: every assertion samples a flat fill, not a blended edge.
            painter.setRenderHint(QPainter::Antialiasing, false);
            scene.render(&painter, region, region);
            painter.end();
            return image;
        };
        auto pixelAt = [](const QImage &image, const QPointF &point) {
            return image.pixelColor(qRound(point.x()), qRound(point.y()));
        };

        for (auto theme : {EditorTheme::Theme::Light, EditorTheme::Theme::Dark}) {
            const QImage image = render(theme);
            const auto &c = EditorTheme::colors(theme);
            // Inside the first node body: below the header band, clear of the text rows at
            // y 30..84 and of the port circles on the left and right edge.
            QCOMPARE(pixelAt(image, firstItem->scenePos() + QPointF(150, 70)), c.nodeBody);
            // Empty canvas past both nodes, and off the grid lines drawn every 24 units.
            QCOMPARE(pixelAt(image, QPointF(590, 290)), c.canvas);
            // The edge runs from (180, 48) to (320, 48), so its midpoint lies on the stroke.
            const QPointF midpoint = edge->path().pointAtPercent(0.5);
            QCOMPARE(qRound(midpoint.y()), 48);
            QCOMPARE(pixelAt(image, midpoint), c.edge);
        }
    }
    // Reapplied after every slot: a failing assertion above must not leave the rest of the
    // binary running under the dark theme.
    void cleanup() { EditorTheme::setTheme(*qApp, EditorTheme::Theme::Light); }
    void disabledControlsDoNotLookLikeEnabledOnes() {
        for (auto theme : {EditorTheme::Theme::Light, EditorTheme::Theme::Dark}) {
            EditorTheme::setTheme(*qApp, theme);
            const auto &c = EditorTheme::colors(theme);
            // The disabled state has to stay visible, so it may not reuse the enabled surface.
            QVERIFY(c.disabledSurface != c.surface);
            QVERIFY(c.disabledText != c.text);

            QWidget container;
            auto *layout = new QVBoxLayout(&container);
            auto *enabled = new QPushButton(&container);
            auto *disabled = new QPushButton(&container);
            disabled->setEnabled(false);
            enabled->setFixedSize(120, 32);
            disabled->setFixedSize(120, 32);
            layout->addWidget(enabled);
            layout->addWidget(disabled);
            container.resize(160, 96);
            container.show();
            QTest::qWait(20);
            const QImage shot = container.grab().toImage();
            QCOMPARE(shot.pixelColor(enabled->geometry().center()), c.surface);
            QCOMPARE(shot.pixelColor(disabled->geometry().center()), c.disabledSurface);
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
