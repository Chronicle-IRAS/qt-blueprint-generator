#include <QtTest/QtTest>

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDockWidget>
#include <QFormLayout>
#include <QGraphicsView>
#include <QGroupBox>
#include <QHash>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QStyleOptionGraphicsItem>
#include <QTemporaryDir>
#include <QTableWidget>
#include <QTimer>

#include "app/main_window.h"
#include "editor/blueprint_scene.h"
#include "editor/node_item.h"
#include "editor/node_properties_editor.h"
#include "ui/theme.h"

namespace {

constexpr int CardHeadHeight = 28;
constexpr int CardOffset = 10;

// Lowest row of the card head that carries text ink, or -1 when the head only shows its
// background. Antialiased pixels count as ink when they are closer to the header text
// colour than to the header background.
int lowestHeadTextRow(NodeItem *item)
{
    QImage image(220, 120, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.translate(CardOffset, CardOffset);
    QStyleOptionGraphicsItem option;
    option.state = QStyle::State_None;
    item->paint(&painter, &option, nullptr);
    painter.end();

    const QColor head = EditorTheme::colors().nodeHeader;
    const QColor text = EditorTheme::colors().nodeHeaderText;
    const int fullDistance = qAbs(text.red() - head.red()) + qAbs(text.green() - head.green())
                             + qAbs(text.blue() - head.blue());
    for (int row = CardHeadHeight - 1; row >= 0; --row) {
        for (int x = CardOffset + 12; x < CardOffset + 170; ++x) {
            const QRgb pixel = image.pixel(x, CardOffset + row);
            if (qAlpha(pixel) == 0) {
                continue;
            }
            const int headDistance = qAbs(qRed(pixel) - head.red())
                                     + qAbs(qGreen(pixel) - head.green())
                                     + qAbs(qBlue(pixel) - head.blue());
            if (headDistance > fullDistance / 2) {
                return row;
            }
        }
    }
    return -1;
}

struct MenuTexts {
    bool opened = false;
    QStringList entries;
    QHash<QString, QStringList> submenus;
};

// Opens a real context menu and records its localized entry texts.
MenuTexts openContextMenu(QWidget *widget, const QPoint &position)
{
    MenuTexts captured;
    QTimer::singleShot(0, [&captured] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu) {
            return;
        }
        captured.opened = true;
        for (QAction *action : menu->actions()) {
            if (action->isSeparator()) {
                captured.entries.append(QStringLiteral("---"));
                continue;
            }
            captured.entries.append(action->text());
            if (QMenu *submenu = action->menu()) {
                QStringList submenuEntries;
                for (QAction *submenuAction : submenu->actions()) {
                    submenuEntries.append(submenuAction->text());
                }
                captured.submenus.insert(action->text(), submenuEntries);
            }
        }
        menu->close();
    });
    QContextMenuEvent event(QContextMenuEvent::Mouse, position, widget->mapToGlobal(position));
    QApplication::sendEvent(widget, &event);
    return captured;
}

} // namespace

class LanguageSwitchTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void switchesBetweenEnglishAndChineseAndPersistsChoice();
    void refreshesExistingNodeTooltips();
    void showsReadOnlyNodeTypeAndFollowsLanguageSwitch();
    void nodeCardHidesTheTypeCaptionWhenNameIsTheTypeName_data();
    void nodeCardHidesTheTypeCaptionWhenNameIsTheTypeName();
    void contextMenusFollowTheLanguageSwitch();

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

void LanguageSwitchTest::refreshesExistingNodeTooltips()
{
    MainWindow window;
    QVERIFY(window.setLanguage(QStringLiteral("en")));
    BlueprintNode node;
    node.id = QStringLiteral("tooltip-node");
    node.type = NodeType::LogicModule;
    node.name = QStringLiteral("Processor");
    node.description = QStringLiteral("User description");
    node.inputs = {{QStringLiteral("request"), QStringLiteral("string"), {}}};
    node.outputs = {{QStringLiteral("response"), QStringLiteral("string"), {}}};
    const QPointF position(123, 234);
    QVERIFY(window.scene()->addNode(node, position));
    auto *item = window.scene()->nodeItem(node.id);
    QVERIFY(item);
    const auto document = window.document();
    const int undoCount = window.scene()->undoStack()->count();
    QCOMPARE(item->toolTip(), QStringLiteral("Processor\nType: Logic Module\nUser description\nInput: request\nOutput: response"));
    QVERIFY(window.setLanguage(QStringLiteral("zh_CN")));
    QCOMPARE(item->toolTip(), QStringLiteral("Processor\n类型：逻辑模块\nUser description\n输入：request\n输出：response"));
    QVERIFY(window.setLanguage(QStringLiteral("en")));
    QCOMPARE(item->toolTip(), QStringLiteral("Processor\nType: Logic Module\nUser description\nInput: request\nOutput: response"));
    QCOMPARE(window.scene()->nodeItem(node.id), item);
    QCOMPARE(item->pos(), position);
    QCOMPARE(window.document(), document);
    QCOMPARE(window.scene()->undoStack()->count(), undoCount);
}

void LanguageSwitchTest::showsReadOnlyNodeTypeAndFollowsLanguageSwitch()
{
    MainWindow window;
    QVERIFY(window.setLanguage(QStringLiteral("en")));
    QVERIFY(window.addNodeOfType(NodeType::LogicModule));
    const QString id = window.document().nodes.constFirst().id;
    window.scene()->nodeItem(id)->setSelected(true);

    auto *editor = window.findChild<NodePropertiesEditor *>(
        QStringLiteral("inspectorNodePropertiesEditor"));
    QVERIFY(editor);
    auto *typeValue = editor->findChild<QLabel *>(QStringLiteral("nodeTypeValue"));
    auto *form = editor->findChild<QFormLayout *>();
    QVERIFY(typeValue && form);
    auto *typeLabel = qobject_cast<QLabel *>(form->labelForField(typeValue));
    QVERIFY(typeLabel);
    QCOMPARE(typeLabel->text(), QStringLiteral("Type"));
    QCOMPARE(typeValue->text(), QStringLiteral("Logic Module"));

    auto *nameEdit = window.findChild<QLineEdit *>(QStringLiteral("nodeNameEdit"));
    auto *applyButton = window.findChild<QPushButton *>(
        QStringLiteral("applyNodePropertiesButton"));
    QVERIFY(nameEdit && applyButton);
    nameEdit->setText(QStringLiteral("LoginService"));
    QTest::mouseClick(applyButton, Qt::LeftButton);
    QCOMPARE(window.document().nodes.constFirst().name, QStringLiteral("LoginService"));
    QCOMPARE(window.document().nodes.constFirst().type, NodeType::LogicModule);
    QCOMPARE(typeValue->text(), QStringLiteral("Logic Module"));

    QVERIFY(window.setLanguage(QStringLiteral("zh_CN")));
    QCOMPARE(typeValue->text(), QStringLiteral("逻辑模块"));
    QCOMPARE(typeLabel->text(), QStringLiteral("类型"));

    QVERIFY(window.setLanguage(QStringLiteral("en")));
    QCOMPARE(typeValue->text(), QStringLiteral("Logic Module"));
    QCOMPARE(typeLabel->text(), QStringLiteral("Type"));
}

void LanguageSwitchTest::nodeCardHidesTheTypeCaptionWhenNameIsTheTypeName_data()
{
    QTest::addColumn<NodeType>("type");
    QTest::addColumn<QString>("englishType");
    QTest::addColumn<QString>("chineseType");

    QTest::newRow("Start") << NodeType::Start << QStringLiteral("Start")
                           << QStringLiteral("开始");
    QTest::newRow("End") << NodeType::End << QStringLiteral("End")
                         << QStringLiteral("结束");
    QTest::newRow("UiPage") << NodeType::UiPage << QStringLiteral("UI Page")
                            << QStringLiteral("界面页面");
    QTest::newRow("LogicModule") << NodeType::LogicModule << QStringLiteral("Logic Module")
                                 << QStringLiteral("逻辑模块");
    QTest::newRow("Decision") << NodeType::Decision << QStringLiteral("Decision")
                              << QStringLiteral("判断");
    QTest::newRow("ExternalCode") << NodeType::ExternalCode
                                  << QStringLiteral("External Code") << QStringLiteral("外部代码");
}

void LanguageSwitchTest::nodeCardHidesTheTypeCaptionWhenNameIsTheTypeName()
{
    QFETCH(NodeType, type);
    QFETCH(QString, englishType);
    QFETCH(QString, chineseType);

    MainWindow window;
    QVERIFY(window.setLanguage(QStringLiteral("en")));
    auto addNode = [&window, type](const QString &id, const QString &name,
                                   const QPointF &position) {
        BlueprintNode node;
        node.id = id;
        node.type = type;
        node.name = name;
        return window.scene()->addNode(node, position);
    };
    // The default name a node gets while the UI is English, the one it gets while the UI
    // is Chinese, and a user chosen name.
    QVERIFY(addNode(QStringLiteral("english-default"), englishType, {}));
    QVERIFY(addNode(QStringLiteral("chinese-default"), chineseType, QPointF(240, 0)));
    QVERIFY(addNode(QStringLiteral("renamed"), QStringLiteral("LoginService"), QPointF(480, 0)));

    NodeItem *englishDefault = window.scene()->nodeItem(QStringLiteral("english-default"));
    NodeItem *chineseDefault = window.scene()->nodeItem(QStringLiteral("chinese-default"));
    NodeItem *renamed = window.scene()->nodeItem(QStringLiteral("renamed"));
    QVERIFY(englishDefault && chineseDefault && renamed);

    const int englishHead = lowestHeadTextRow(englishDefault);
    QVERIFY(englishHead >= 0);
    // Only the card whose name repeats the English type label stays on a single line.
    QVERIFY(lowestHeadTextRow(chineseDefault) > englishHead + 3);
    QVERIFY(lowestHeadTextRow(renamed) > englishHead + 3);
    QVERIFY(englishDefault->toolTip().contains(QStringLiteral("Type: ") + englishType));
    QVERIFY(chineseDefault->toolTip().contains(QStringLiteral("Type: ") + englishType));
    QVERIFY(renamed->toolTip().contains(QStringLiteral("Type: ") + englishType));

    // The inspector keeps reporting the type even while the card hides the duplicate.
    englishDefault->setSelected(true);
    auto *editor = window.findChild<NodePropertiesEditor *>(
        QStringLiteral("inspectorNodePropertiesEditor"));
    QVERIFY(editor);
    auto *typeValue = editor->findChild<QLabel *>(QStringLiteral("nodeTypeValue"));
    QVERIFY(typeValue);
    QCOMPARE(typeValue->text(), englishType);

    QVERIFY(window.setLanguage(QStringLiteral("zh_CN")));
    const int chineseHead = lowestHeadTextRow(chineseDefault);
    QVERIFY(chineseHead >= 0);
    QVERIFY(lowestHeadTextRow(englishDefault) > chineseHead + 3);
    QVERIFY(lowestHeadTextRow(renamed) > chineseHead + 3);
    QCOMPARE(typeValue->text(), chineseType);
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
        auto *inputsGroup = window.findChild<QGroupBox *>(
            QStringLiteral("inspectorNodeInputsEditor"));
        QVERIFY(languageMenu && viewMenu && englishAction && chineseAction && propertiesAction
                && buildDockAction && resetLayoutAction && toolbarBuildAction
                && toolbarExportAction && propertiesDock && buildButton && inputsGroup);

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
        QCOMPARE(inputsGroup->title(), QStringLiteral("输入"));
        auto *addInput = inputsGroup->findChild<QPushButton *>(QStringLiteral("addPortButton"));
        QVERIFY(addInput);
        QCOMPARE(addInput->text(), QStringLiteral("添加输入"));
        QVERIFY(chineseAction->isChecked());
        QVERIFY(window.addNodeOfType(NodeType::LogicModule));
        QCOMPARE(window.document().nodes.constFirst().name, QStringLiteral("逻辑模块"));
        auto *nameDraft = window.findChild<QLineEdit *>(QStringLiteral("nodeNameEdit"));
        auto *inputsTable = inputsGroup->findChild<QTableWidget *>(QStringLiteral("portItemsTable"));
        QVERIFY(nameDraft && inputsTable);
        window.show();
        QApplication::processEvents();
        bool dialogOpened = false;
        QString dialogTitle;
        QString saveText;
        QString cancelText;
        bool dialogTypeCaptured = false;
        QString dialogTypeText;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) {
                return;
            }
            dialogOpened = true;
            dialogTitle = dialog->windowTitle();
            if (auto *typeValue = dialog->findChild<QLabel *>(
                    QStringLiteral("directNodeTypeValue"))) {
                dialogTypeCaptured = true;
                dialogTypeText = typeValue->text();
            }
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
        QVERIFY(dialogTypeCaptured);
        QCOMPARE(dialogTypeText, QStringLiteral("逻辑模块"));

        nameDraft->setText(QStringLiteral("未提交草稿"));
        QTest::mouseClick(addInput, Qt::LeftButton);
        inputsTable->item(0, 0)->setText(QStringLiteral("draft-input"));
        englishAction->trigger();
        QCOMPARE(window.currentLanguage(), QStringLiteral("en"));
        QCOMPARE(window.windowTitle(), QStringLiteral("Blueprint Editor"));
        QCOMPARE(languageMenu->title(), QStringLiteral("Language"));
        QCOMPARE(propertiesDock->windowTitle(), QStringLiteral("Properties"));
        QCOMPARE(buildButton->text(), QStringLiteral("Build"));
        QCOMPARE(nameDraft->text(), QStringLiteral("未提交草稿"));
        QCOMPARE(inputsTable->rowCount(), 1);
        QCOMPARE(inputsTable->item(0, 0)->text(), QStringLiteral("draft-input"));
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

void LanguageSwitchTest::contextMenusFollowTheLanguageSwitch()
{
    MainWindow window;
    QVERIFY(window.setLanguage(QStringLiteral("en")));
    window.show();
    QApplication::processEvents();
    QGraphicsView *view = window.graphicsView();
    // The scene is still empty here, so the view centre is a canvas position.
    const QPoint centre = view->viewport()->rect().center();

    MenuTexts menu = openContextMenu(view->viewport(), centre);
    QVERIFY(menu.opened);
    QCOMPARE(menu.entries,
             QStringList({QStringLiteral("Add node"), QStringLiteral("---"),
                          QStringLiteral("Select All"), QStringLiteral("Fit View"),
                          QStringLiteral("Reset View")}));
    QCOMPARE(menu.submenus.value(QStringLiteral("Add node")),
             QStringList({QStringLiteral("Start"), QStringLiteral("End"), QStringLiteral("UI Page"),
                          QStringLiteral("Logic Module"), QStringLiteral("Decision"),
                          QStringLiteral("External Code")}));

    QVERIFY(window.addNodeOfType(NodeType::LogicModule));
    const QString id = window.document().nodes.constFirst().id;
    const QPoint nodePoint = view->mapFromScene(
        window.scene()->nodeItem(id)->sceneBoundingRect().center());
    menu = openContextMenu(view->viewport(), nodePoint);
    QVERIFY(menu.opened);
    QCOMPARE(menu.entries,
             QStringList({QStringLiteral("Edit node"), QStringLiteral("Generate selected node"),
                          QStringLiteral("---"), QStringLiteral("Delete")}));

    QVERIFY(window.setLanguage(QStringLiteral("zh_CN")));
    menu = openContextMenu(view->viewport(), nodePoint);
    QVERIFY(menu.opened);
    QCOMPARE(menu.entries,
             QStringList({QStringLiteral("编辑节点"), QStringLiteral("生成选中节点"),
                          QStringLiteral("---"), QStringLiteral("删除")}));

    const QPoint blank(16, 16);
    menu = openContextMenu(view->viewport(), blank);
    QVERIFY(menu.opened);
    QCOMPARE(menu.entries,
             QStringList({QStringLiteral("添加节点"), QStringLiteral("---"), QStringLiteral("全选"),
                          QStringLiteral("适应视图"), QStringLiteral("重置视图")}));
    QCOMPARE(menu.submenus.value(QStringLiteral("添加节点")),
             QStringList({QStringLiteral("开始"), QStringLiteral("结束"), QStringLiteral("界面页面"),
                          QStringLiteral("逻辑模块"), QStringLiteral("判断"),
                          QStringLiteral("外部代码")}));

    QVERIFY(window.setLanguage(QStringLiteral("en")));
    menu = openContextMenu(view->viewport(), blank);
    QVERIFY(menu.opened);
    QCOMPARE(menu.entries.constFirst(), QStringLiteral("Add node"));
}

QTEST_MAIN(LanguageSwitchTest)

#include "tst_language_switch.moc"
