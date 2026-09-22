#include "app/main_window.h"
#include "ui/node_type_display.h"
#include "ui/theme.h"

#include "app/ai_settings_dialog.h"
#include "app/candidate_review_dialog.h"
#include "app/validation_diagnostics_dialog.h"
#include <QUuid>

#include "editor/blueprint_scene.h"
#include "editor/edge_item.h"
#include "editor/node_item.h"
#include "editor/node_properties_editor.h"
#include "workspace/build_service.h"
#include "workspace/project_exporter.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDir>
#include <QEvent>
#include <QFormLayout>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>

namespace {

QString nodeTypeActionObjectName(NodeType type)
{
    switch (type) {
    case NodeType::Start:
        return QStringLiteral("addStartNodeAction");
    case NodeType::End:
        return QStringLiteral("addEndNodeAction");
    case NodeType::UiPage:
        return QStringLiteral("addUIPageNodeAction");
    case NodeType::LogicModule:
        return QStringLiteral("addLogicModuleNodeAction");
    case NodeType::Decision:
        return QStringLiteral("addDecisionNodeAction");
    case NodeType::ExternalCode:
        return QStringLiteral("addExternalCodeNodeAction");
    }
    return {};
}

QString buildStageName(BuildStage stage)
{
    return stage == BuildStage::Configure ? QObject::tr("configure") : QObject::tr("build");
}

QString encodeCommandArguments(const QStringList &arguments)
{
    QStringList encoded;
    for (QString argument : arguments) {
        argument.replace(QLatin1Char('"'), QStringLiteral("\"\"\""));
        if (argument.isEmpty() || argument.contains(QLatin1Char(' ')) || argument.contains(QLatin1Char('\t')))
            argument = QLatin1Char('"') + argument + QLatin1Char('"');
        encoded.append(argument);
    }
    return encoded.join(QLatin1Char(' '));
}

void setFormLabel(QFormLayout *form, QWidget *field, const QString &text)
{
    if (auto *label = qobject_cast<QLabel *>(form->labelForField(field))) {
        label->setText(text);
    }
}

// Single source for the add-node entries of the toolbar menu and the canvas context menu.
const QVector<NodeType> &blueprintNodeTypes()
{
    static const QVector<NodeType> types{
        NodeType::Start,
        NodeType::End,
        NodeType::UiPage,
        NodeType::LogicModule,
        NodeType::Decision,
        NodeType::ExternalCode,
    };
    return types;
}

constexpr qreal MinZoom = 0.25;
constexpr qreal MaxZoom = 3.0;

constexpr qreal clampZoom(qreal zoom)
{
    return std::clamp(zoom, MinZoom, MaxZoom);
}

// Fitting a single node would otherwise zoom far closer than the wheel allows.
void clampViewZoom(QGraphicsView *view)
{
    const qreal zoom = view->transform().m11();
    const qreal clamped = clampZoom(zoom);
    if (!qFuzzyCompare(clamped, zoom)) {
        view->scale(clamped / zoom, clamped / zoom);
    }
}

// Fits every node instead of scene()->itemsBoundingRect(), so one far away edge or label
// cannot shrink the whole blueprint into a corner.
void fitViewToNodeItems(QGraphicsView *view)
{
    QRectF bounds;
    for (QGraphicsItem *item : view->scene()->items()) {
        if (!dynamic_cast<NodeItem *>(item)) {
            continue;
        }
        bounds = bounds.isNull() ? item->sceneBoundingRect() : bounds.united(item->sceneBoundingRect());
    }
    if (bounds.isNull()) {
        return;
    }
    constexpr qreal Margin = 24.0;
    view->fitInView(bounds.adjusted(-Margin, -Margin, Margin, Margin), Qt::KeepAspectRatio);
    clampViewZoom(view);
}

void resetCanvasView(QGraphicsView *view)
{
    view->resetTransform();
    view->centerOn(view->scene()->sceneRect().center());
}

class NodeEditDialog final : public QDialog
{
public:
    explicit NodeEditDialog(const BlueprintNode &node, QWidget *parent = nullptr)
        : QDialog(parent)
        , m_node(node)
    {
        setObjectName(QStringLiteral("nodeEditDialog"));
        setWindowTitle(MainWindow::tr("Edit node"));
        resize(560, 680);

        auto *layout = new QVBoxLayout(this);
        m_editor = new NodePropertiesEditor(QStringLiteral("direct"), this);
        m_editor->setNode(node);
        auto *scrollArea = new QScrollArea(this);
        scrollArea->setObjectName(QStringLiteral("directNodePropertiesScrollArea"));
        scrollArea->setWidgetResizable(true);
        scrollArea->setWidget(m_editor);
        layout->addWidget(scrollArea);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,
                                             Qt::Horizontal, this);
        auto *saveButton = buttons->button(QDialogButtonBox::Save);
        saveButton->setObjectName(QStringLiteral("saveNodeEditButton"));
        saveButton->setText(MainWindow::tr("Save"));
        auto *cancelButton = buttons->button(QDialogButtonBox::Cancel);
        cancelButton->setObjectName(QStringLiteral("cancelNodeEditButton"));
        cancelButton->setText(MainWindow::tr("Cancel"));
        layout->addWidget(buttons);

        connect(buttons, &QDialogButtonBox::accepted, this, [this] {
            BlueprintNode updated = m_node;
            m_editor->applyTo(&updated);
            m_node = updated;
            accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

    const BlueprintNode &node() const
    {
        return m_node;
    }

private:
    BlueprintNode m_node;
    NodePropertiesEditor *m_editor = nullptr;
};

class BlueprintView final : public QGraphicsView
{
public:
    explicit BlueprintView(QGraphicsScene *scene, QWidget *parent = nullptr)
        : QGraphicsView(scene, parent)
    {
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        endPan();
        // Dragging the empty canvas pans the view. Nodes, edges and their ports keep their
        // own gestures, and holding a selection modifier keeps Qt's rubber band.
        if (event->button() == Qt::LeftButton && !rubberBandRequested(event)
            && isEmptyCanvas(event->position().toPoint())) {
            m_panArmed = true;
            m_panAnchor = event->position().toPoint();
            viewport()->setCursor(Qt::ClosedHandCursor);
            // The press still has to reach the scene, so a click keeps clearing the selection
            // and focusing the canvas, but Qt must not arm its rubber band for this drag.
            const DragMode mode = dragMode();
            setDragMode(NoDrag);
            QGraphicsView::mousePressEvent(event);
            setDragMode(mode);
            return;
        }
        QGraphicsView::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_panArmed || !event->buttons().testFlag(Qt::LeftButton)) {
            // A release that never reached us must not leave the view panning.
            endPan();
            QGraphicsView::mouseMoveEvent(event);
            return;
        }
        const QPoint position = event->position().toPoint();
        if (!m_panStarted
            && (position - m_panAnchor).manhattanLength() < QApplication::startDragDistance()) {
            event->accept();
            return;
        }
        m_panStarted = true;
        const QPoint delta = position - m_panAnchor;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        m_panAnchor = position;
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        endPan();
        QGraphicsView::mouseReleaseEvent(event);
    }

    void wheelEvent(QWheelEvent *event) override
    {
        if (event->angleDelta().y() == 0) {
            QGraphicsView::wheelEvent(event);
            return;
        }
        const qreal factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
        const qreal current = transform().m11();
        const qreal target = clampZoom(current * factor);
        const QPoint cursor = event->position().toPoint();
        const QPointF anchoredScenePoint = mapToScene(cursor);
        if (!qFuzzyCompare(current, target)) {
            // The view transform is a plain scale: panning moves the scroll bars, so replacing
            // the matrix keeps the zoom exactly at the requested value inside the limits.
            setTransform(QTransform::fromScale(target, target));
        }
        // Zooming keeps the scene point under the cursor under the cursor. Qt's AnchorUnderMouse
        // follows the global cursor instead of this event, and is unreliable while panning.
        const QPoint drift = mapFromScene(anchoredScenePoint) - cursor;
        if (!drift.isNull()) {
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() + drift.x());
            verticalScrollBar()->setValue(verticalScrollBar()->value() + drift.y());
        }
        if (m_panStarted) {
            // The next pan sample starts where the cursor is now, or the zoom would show up as
            // a jump on the next mouse move.
            m_panAnchor = cursor;
        }
        event->accept();
    }

private:
    static bool rubberBandRequested(QMouseEvent *event)
    {
        return (event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) != 0;
    }

    // Only the empty canvas pans: a press on a node, an edge (including the widened hit shape)
    // or any other item belongs to that item.
    bool isEmptyCanvas(const QPoint &viewportPosition) const
    {
        return scene() && !scene()->itemAt(mapToScene(viewportPosition), transform());
    }

    void endPan()
    {
        const bool wasPanning = m_panArmed || m_panStarted;
        m_panArmed = false;
        m_panStarted = false;
        if (wasPanning) {
            viewport()->unsetCursor();
        }
    }

    QPoint m_panAnchor;
    bool m_panArmed = false;
    bool m_panStarted = false;
};

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : MainWindow(GenerationController::ClientFactory{}, parent)
{
}

MainWindow::MainWindow(GenerationController::ClientFactory factory, QWidget *parent)
    : QMainWindow(parent)
{
    m_document.projectId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_document.projectName = QStringLiteral("BlueprintProject");
    m_document.target = QStringLiteral("qt6-widgets-cpp17-cmake");
    setWindowTitle(tr("Blueprint Editor"));
    resize(1100, 700);

    m_scene = new BlueprintScene(&m_document, this);
    m_view = new BlueprintView(m_scene, this);
    m_view->setDragMode(QGraphicsView::RubberBandDrag);
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    setCentralWidget(m_view);

    m_blueprintToolbar = addToolBar(tr("Blueprint"));
    m_blueprintToolbar->setObjectName(QStringLiteral("blueprintToolbar"));
    m_addNodeButton = new QToolButton(m_blueprintToolbar);
    m_addNodeButton->setObjectName(QStringLiteral("addNodeButton"));
    m_addNodeButton->setText(tr("Add node"));
    m_addNodeButton->setPopupMode(QToolButton::InstantPopup);
    m_addNodeMenu = new QMenu(m_addNodeButton);
    for (const NodeType type : blueprintNodeTypes()) {
        QAction *action = m_addNodeMenu->addAction(nodeTypeDisplayName(type));
        action->setObjectName(nodeTypeActionObjectName(type));
        m_addNodeActions.append({type, action});
        connect(action, &QAction::triggered, this, [this, type] { addNodeOfType(type); });
    }
    m_addNodeButton->setMenu(m_addNodeMenu);
    m_blueprintToolbar->addWidget(m_addNodeButton);
    m_connectionLabelEdit = new QLineEdit(m_blueprintToolbar);
    m_connectionLabelEdit->setObjectName(QStringLiteral("connectionLabelEdit"));
    m_connectionLabelEdit->setPlaceholderText(tr("Edge label (optional)"));
    m_connectionLabelEdit->setToolTip(tr("Label for the next source-to-target connection"));
    m_connectionLabelEdit->setMaximumWidth(220);
    m_blueprintToolbar->addWidget(m_connectionLabelEdit);
    m_deleteAction = m_blueprintToolbar->addAction(tr("Delete"));
    m_deleteAction->setObjectName(QStringLiteral("deleteSelectionAction"));
    m_connectAction = m_blueprintToolbar->addAction(tr("Connect: choose source then target"));
    m_connectAction->setObjectName(QStringLiteral("beginConnectionAction"));
    m_cancelConnectionAction = new QAction(tr("Cancel connection"), this);
    m_cancelConnectionAction->setObjectName(QStringLiteral("cancelConnectionAction"));
    m_cancelConnectionAction->setShortcut(QKeySequence(Qt::Key_Escape));
    m_cancelConnectionAction->setShortcutContext(Qt::WindowShortcut);
    m_blueprintToolbar->addAction(m_cancelConnectionAction);
    m_blueprintToolbar->addSeparator();
    m_toolbarUndoAction = m_blueprintToolbar->addAction(tr("Undo"));
    m_toolbarRedoAction = m_blueprintToolbar->addAction(tr("Redo"));
    m_blueprintToolbar->addSeparator();
    m_toolbarBuildAction = m_blueprintToolbar->addAction(tr("Build"));
    m_toolbarBuildAction->setObjectName(QStringLiteral("toolbarBuildAction"));
    m_toolbarExportAction = m_blueprintToolbar->addAction(tr("Export"));
    m_toolbarExportAction->setObjectName(QStringLiteral("toolbarExportAction"));

    m_editMenu = menuBar()->addMenu(tr("Edit"));
    m_editMenu->setObjectName(QStringLiteral("editMenu"));
    m_menuUndoAction = m_editMenu->addAction(tr("Undo"));
    m_menuRedoAction = m_editMenu->addAction(tr("Redo"));

    m_viewMenu = menuBar()->addMenu(tr("View"));
    m_viewMenu->setObjectName(QStringLiteral("viewMenu"));

    // Canvas entries live in the canvas context menu only; the issue keeps the menu bar
    // and toolbar structure untouched.
    m_selectAllAction = new QAction(tr("Select All"), this);
    m_selectAllAction->setObjectName(QStringLiteral("selectAllCanvasAction"));
    connect(m_selectAllAction, &QAction::triggered, m_scene, &BlueprintScene::selectAllItems);
    m_fitViewAction = new QAction(tr("Fit View"), this);
    m_fitViewAction->setObjectName(QStringLiteral("fitCanvasViewAction"));
    connect(m_fitViewAction, &QAction::triggered, this, [this] { fitViewToNodeItems(m_view); });
    m_resetViewAction = new QAction(tr("Reset View"), this);
    m_resetViewAction->setObjectName(QStringLiteral("resetCanvasViewAction"));
    connect(m_resetViewAction, &QAction::triggered, this, [this] { resetCanvasView(m_view); });

    m_aiMenu = menuBar()->addMenu(tr("AI"));
    m_aiMenu->setObjectName(QStringLiteral("aiMenu"));
    m_aiSettingsAction = m_aiMenu->addAction(tr("AI Settings..."));
    m_aiSettingsAction->setObjectName(QStringLiteral("aiSettingsAction"));
    m_generateAction = m_aiMenu->addAction(tr("Generate selected node"));
    m_generateAction->setObjectName(QStringLiteral("generateSelectedNodeAction"));
    m_cancelGenerationAction = m_aiMenu->addAction(tr("Cancel generation"));
    m_cancelGenerationAction->setObjectName(QStringLiteral("cancelGenerationAction"));
    m_blueprintToolbar->addSeparator();
    m_blueprintToolbar->addAction(m_generateAction);
    m_blueprintToolbar->widgetForAction(m_generateAction)->setProperty("role", "primary");
    m_blueprintToolbar->addAction(m_cancelGenerationAction);
    m_generationStatus = new QLabel(this);
    m_generationStatus->setObjectName(QStringLiteral("generationStatus"));
    m_generationStatus->setTextFormat(Qt::PlainText);
    statusBar()->addPermanentWidget(m_generationStatus);

    m_languageMenu = menuBar()->addMenu(tr("Language"));
    m_languageMenu->setObjectName(QStringLiteral("languageMenu"));
    auto *languageGroup = new QActionGroup(this);
    languageGroup->setExclusive(true);
    m_englishLanguageAction = m_languageMenu->addAction(tr("English"));
    m_englishLanguageAction->setObjectName(QStringLiteral("languageEnglishAction"));
    m_englishLanguageAction->setCheckable(true);
    languageGroup->addAction(m_englishLanguageAction);
    m_chineseLanguageAction = m_languageMenu->addAction(tr("Chinese"));
    m_chineseLanguageAction->setObjectName(QStringLiteral("languageChineseAction"));
    m_chineseLanguageAction->setCheckable(true);
    languageGroup->addAction(m_chineseLanguageAction);

    m_propertiesDock = new QDockWidget(tr("Properties"), this);
    m_propertiesDock->setObjectName(QStringLiteral("propertiesDock"));
    auto *propertyWidget = new QWidget(m_propertiesDock);
    auto *propertyLayout = new QVBoxLayout(propertyWidget);
    m_nodePropertiesEditor = new NodePropertiesEditor(QStringLiteral("inspector"), propertyWidget);
    auto *propertyScrollArea = new QScrollArea(propertyWidget);
    propertyScrollArea->setObjectName(QStringLiteral("nodePropertiesScrollArea"));
    propertyScrollArea->setWidgetResizable(true);
    propertyScrollArea->setWidget(m_nodePropertiesEditor);
    propertyLayout->addWidget(propertyScrollArea);
    m_applyPropertiesButton = new QPushButton(tr("Apply"), propertyWidget);
    m_applyPropertiesButton->setObjectName(QStringLiteral("applyNodePropertiesButton"));
    m_applyPropertiesButton->setProperty("role", "primary");
    propertyLayout->addWidget(m_applyPropertiesButton);
    m_propertiesDock->setWidget(propertyWidget);
    addDockWidget(Qt::RightDockWidgetArea, m_propertiesDock);

    m_buildDock = new QDockWidget(tr("Build and export"), this);
    m_buildDock->setObjectName(QStringLiteral("buildExportDock"));
    auto *buildWidget = new QWidget(m_buildDock);
    auto *buildLayout = new QVBoxLayout(buildWidget);
    m_buildForm = new QFormLayout;
    m_workspacePathEdit = new QLineEdit(QDir::currentPath(), buildWidget);
    m_workspacePathEdit->setObjectName(QStringLiteral("workspacePathEdit"));
    m_workspacePathEdit->setToolTip(tr("Workspace root containing generated-project"));
    m_buildDirectoryEdit = new QLineEdit(QDir(QDir::currentPath()).filePath(QStringLiteral("build/generated-project")), buildWidget);
    m_buildDirectoryEdit->setObjectName(QStringLiteral("buildDirectoryEdit"));
    m_exportTargetEdit = new QLineEdit(buildWidget);
    m_exportTargetEdit->setObjectName(QStringLiteral("exportTargetEdit"));
    m_cmakeExecutableEdit = new QLineEdit(QStringLiteral("cmake"), buildWidget);
    m_cmakeExecutableEdit->setObjectName(QStringLiteral("cmakeExecutableEdit"));
    m_configureArgumentsEdit = new QLineEdit(buildWidget);
    m_configureArgumentsEdit->setObjectName(QStringLiteral("cmakeConfigureArgumentsEdit"));
    m_configureArgumentsEdit->setPlaceholderText(tr("For example: -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.x/mingw_64"));
    m_buildForm->addRow(tr("Workspace root"), m_workspacePathEdit);
    m_buildForm->addRow(tr("Build directory"), m_buildDirectoryEdit);
    m_buildForm->addRow(tr("Empty export directory"), m_exportTargetEdit);
    m_buildForm->addRow(tr("CMake executable"), m_cmakeExecutableEdit);
    m_buildForm->addRow(tr("Configure arguments"), m_configureArgumentsEdit);
    auto *buildFormContents = new QWidget(buildWidget);
    m_buildForm->setContentsMargins(EditorTheme::SpaceSmall, EditorTheme::SpaceSmall,
                                   EditorTheme::SpaceSmall, EditorTheme::SpaceSmall);
    m_buildForm->setSpacing(EditorTheme::SpaceMedium);
    m_buildForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    buildFormContents->setLayout(m_buildForm);
    auto *buildFormScroll = new QScrollArea(buildWidget);
    buildFormScroll->setObjectName(QStringLiteral("buildFormScrollArea"));
    buildFormScroll->setWidgetResizable(true);
    buildFormScroll->setFrameShape(QFrame::NoFrame);
    buildFormScroll->setMaximumHeight(112);
    buildFormScroll->setWidget(buildFormContents);
    buildLayout->setSpacing(EditorTheme::SpaceMedium);
    buildLayout->addWidget(buildFormScroll);
    auto *buttons = new QHBoxLayout;
    m_buildProjectButton = new QPushButton(tr("Build"), buildWidget);
    m_buildProjectButton->setObjectName(QStringLiteral("buildProjectButton"));
    m_buildProjectButton->setProperty("role", "primary");
    m_exportProjectButton = new QPushButton(tr("Export"), buildWidget);
    m_exportProjectButton->setObjectName(QStringLiteral("exportProjectButton"));
    buttons->addWidget(m_buildProjectButton);
    buttons->addWidget(m_exportProjectButton);
    buttons->addStretch();
    buildLayout->addLayout(buttons);
    m_buildLog = new QPlainTextEdit(buildWidget);
    m_buildLog->setObjectName(QStringLiteral("buildLog"));
    m_buildLog->setReadOnly(true);
    m_buildLog->setFont(EditorTheme::codeFont());
    m_buildLog->setPlaceholderText(tr("Configure, build, and export output appears here."));
    m_buildLog->setMaximumBlockCount(10000);
    buildLayout->addWidget(m_buildLog);
    m_buildDock->setWidget(buildWidget);
    addDockWidget(Qt::BottomDockWidgetArea, m_buildDock);
    resizeDocks({m_propertiesDock}, {340}, Qt::Horizontal);
    resizeDocks({m_buildDock}, {260}, Qt::Vertical);

    m_propertiesDockAction = m_propertiesDock->toggleViewAction();
    m_propertiesDockAction->setObjectName(QStringLiteral("propertiesDockAction"));
    m_buildDockAction = m_buildDock->toggleViewAction();
    m_buildDockAction->setObjectName(QStringLiteral("buildExportDockAction"));
    m_viewMenu->addAction(m_propertiesDockAction);
    m_viewMenu->addAction(m_buildDockAction);
    m_viewMenu->addSeparator();
    m_resetLayoutAction = m_viewMenu->addAction(tr("Reset Layout"));
    m_resetLayoutAction->setObjectName(QStringLiteral("resetLayoutAction"));

    m_buildService = new BuildService(this);
    m_generationController = factory ? new GenerationController(std::move(factory), this)
                                     : new GenerationController(this);
    connect(m_generateAction, &QAction::triggered, this, &MainWindow::startGeneration);
    connect(m_cancelGenerationAction, &QAction::triggered,
            m_generationController, &GenerationController::cancel);
    connect(m_generationController, &GenerationController::stateChanged,
            this, [this] {
        updateGenerationUi();
        showValidationDiagnostics();
    });
    connect(m_generationController, &GenerationController::candidateReady, this,
            [this](const CandidateBatch &batch) {
        auto *dialog = new CandidateReviewDialog(m_generationSnapshot, batch.workspace,
                                                 batch.generationId, batch.result, this);
        m_reviewDialog = dialog;
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowModality(Qt::ApplicationModal);
        connect(dialog, &QObject::destroyed, this, [this] {
            m_reviewDialog = nullptr;
            updateGenerationUi();
        });
        dialog->show();
        updateGenerationUi();
    });
    connect(m_workspacePathEdit, &QLineEdit::textChanged,
            this, &MainWindow::invalidateGenerationContext);

    connect(m_deleteAction, &QAction::triggered, this, [this] { m_scene->deleteSelectedItems(); });
    connect(m_connectAction, &QAction::triggered, this, [this] {
        m_scene->beginConnection(m_connectionLabelEdit->text());
        statusBar()->showMessage(tr("Choose source node, then target node"));
    });
    connect(m_connectionLabelEdit, &QLineEdit::textChanged,
            m_scene, &BlueprintScene::setDragConnectionLabel);
    m_scene->setDragConnectionLabel(m_connectionLabelEdit->text());
    connect(m_cancelConnectionAction, &QAction::triggered, this, [this] { cancelConnection(); });
    for (QAction *action : {m_toolbarUndoAction, m_menuUndoAction}) {
        connect(action, &QAction::triggered, m_scene->undoStack(), &QUndoStack::undo);
        action->setEnabled(m_scene->undoStack()->canUndo());
    }
    for (QAction *action : {m_toolbarRedoAction, m_menuRedoAction}) {
        connect(action, &QAction::triggered, m_scene->undoStack(), &QUndoStack::redo);
        action->setEnabled(m_scene->undoStack()->canRedo());
    }
    connect(m_scene->undoStack(), &QUndoStack::canUndoChanged, this, [this](bool enabled) {
        m_toolbarUndoAction->setEnabled(enabled);
        m_menuUndoAction->setEnabled(enabled);
    });
    connect(m_scene->undoStack(), &QUndoStack::canRedoChanged, this, [this](bool enabled) {
        m_toolbarRedoAction->setEnabled(enabled);
        m_menuRedoAction->setEnabled(enabled);
    });
    connect(m_englishLanguageAction, &QAction::triggered, this,
            [this] { setLanguage(QStringLiteral("en")); });
    connect(m_chineseLanguageAction, &QAction::triggered, this,
            [this] { setLanguage(QStringLiteral("zh_CN")); });
    connect(m_aiSettingsAction, &QAction::triggered, this, [this] {
        AiSettingsDialog dialog(this);
        dialog.exec();
    });
    connect(m_applyPropertiesButton, &QPushButton::clicked, this, [this] { applyProperties(); });
    connect(m_buildProjectButton, &QPushButton::clicked, this, [this] { startBuild(); });
    connect(m_exportProjectButton, &QPushButton::clicked, this, [this] { exportProject(); });
    connect(m_toolbarBuildAction, &QAction::triggered, this, [this] { startBuild(); });
    connect(m_toolbarExportAction, &QAction::triggered, this, [this] { exportProject(); });
    connect(m_resetLayoutAction, &QAction::triggered, this, [this] { resetWindowLayout(); });
    connect(m_buildService, &BuildService::standardOutput, this,
            [this](BuildStage, const QString &text) { appendBuildLog(text); });
    connect(m_buildService, &BuildService::standardError, this,
            [this](BuildStage stage, const QString &text) {
                appendBuildLog(QStringLiteral("[%1 stderr]\n%2").arg(buildStageName(stage), text));
            });
    connect(m_buildService, &BuildService::stageFinished, this,
            [this](BuildStage stage, int exitCode) {
                appendBuildLog(tr("[%1] exit code %2\n").arg(buildStageName(stage)).arg(exitCode));
            });
    connect(m_buildService, &BuildService::finished, this, [this](const BuildResult &result) {
        m_buildProjectButton->setEnabled(true);
        m_toolbarBuildAction->setEnabled(true);
        appendBuildLog(result.success ? tr("Build finished successfully.\n")
                                      : tr("Build failed: %1\n").arg(result.error));
    });
    connect(m_scene, &BlueprintScene::nodeEditRequested, this,
            [this](const QString &nodeId) { editNodeFromCanvas(nodeId); });
    connect(m_scene, &BlueprintScene::contextMenuRequested, this,
            [this](QGraphicsItem *target, const QPoint &screenPosition, const QPointF &scenePosition) {
                showCanvasContextMenu(target, screenPosition, scenePosition);
            });
    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this] {
        updatePropertyEditor();
        updateGenerationUi();
    });
    m_scene->setSemanticChangeHandler([this] {
        updatePropertyEditor();
        invalidateGenerationContext();
    });
    updatePropertyEditor();

    const QString savedLanguage = QSettings().value(QStringLiteral("ui/language"),
                                                     QStringLiteral("en")).toString();
    if (!applyLanguage(savedLanguage, false)) {
        applyLanguage(QStringLiteral("en"), false);
    }
}

MainWindow::~MainWindow()
{
    if (m_reviewDialog)
        QObject::disconnect(m_reviewDialog, nullptr, this, nullptr);
    QObject::disconnect(m_generationController, nullptr, this, nullptr);
    qApp->removeTranslator(&m_translator);
    if (m_scene) {
        QObject::disconnect(m_scene, nullptr, this, nullptr);
        m_scene->setSemanticChangeHandler({});
    }
}

bool MainWindow::addNodeOfType(NodeType type)
{
    const QPointF center = m_view->mapToScene(m_view->viewport()->rect().center());
    const qsizetype ordinal = m_document.nodes.size();
    return addNodeOfTypeAt(type, center + QPointF((ordinal % 3) * 220.0, (ordinal / 3) * 140.0));
}

bool MainWindow::addNodeOfTypeAt(NodeType type, const QPointF &scenePosition)
{
    int number = 1;
    QString id;
    do {
        id = QStringLiteral("node-%1").arg(number++);
    } while (m_scene->nodeItem(id));

    BlueprintNode node;
    node.id = id;
    node.type = type;
    node.name = nodeTypeDisplayName(type);
    const bool added = m_scene->addNode(node, scenePosition);
    if (added) {
        m_scene->clearSelection();
        m_scene->nodeItem(id)->setSelected(true);
    }
    return added;
}

const BlueprintDocument &MainWindow::document() const
{
    return m_document;
}

BlueprintScene *MainWindow::scene() const
{
    return m_scene;
}

QGraphicsView *MainWindow::graphicsView() const
{
    return m_view;
}

QString MainWindow::currentLanguage() const
{
    return m_currentLanguage;
}

bool MainWindow::setLanguage(const QString &languageCode)
{
    return applyLanguage(languageCode, true);
}

bool MainWindow::applyLanguage(const QString &languageCode, bool persist)
{
    if (languageCode != QStringLiteral("en") && languageCode != QStringLiteral("zh_CN")) {
        return false;
    }

    if (languageCode != m_currentLanguage) {
        if (languageCode == QStringLiteral("zh_CN")) {
            if (!m_translator.load(QStringLiteral(":/i18n/BlueprintEditor_zh_CN.qm"))) {
                return false;
            }
            m_currentLanguage = languageCode;
            qApp->installTranslator(&m_translator);
        } else {
            m_currentLanguage = languageCode;
            qApp->removeTranslator(&m_translator);
        }
    }

    if (persist) {
        QSettings().setValue(QStringLiteral("ui/language"), m_currentLanguage);
    }
    updateLanguageActions();
    retranslateUi();
    return true;
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void MainWindow::updateLanguageActions()
{
    m_englishLanguageAction->setChecked(m_currentLanguage == QStringLiteral("en"));
    m_chineseLanguageAction->setChecked(m_currentLanguage == QStringLiteral("zh_CN"));
}

void MainWindow::retranslateUi()
{
    setWindowTitle(tr("Blueprint Editor"));
    m_blueprintToolbar->setWindowTitle(tr("Blueprint"));
    m_addNodeButton->setText(tr("Add node"));
    for (const auto &[type, action] : m_addNodeActions) {
        action->setText(nodeTypeDisplayName(type));
    }
    m_connectionLabelEdit->setPlaceholderText(tr("Edge label (optional)"));
    m_connectionLabelEdit->setToolTip(tr("Label for the next source-to-target connection"));
    m_deleteAction->setText(tr("Delete"));
    m_connectAction->setText(tr("Connect: choose source then target"));
    m_cancelConnectionAction->setText(tr("Cancel connection"));
    m_toolbarUndoAction->setText(tr("Undo"));
    m_toolbarRedoAction->setText(tr("Redo"));
    m_editMenu->setTitle(tr("Edit"));
    m_menuUndoAction->setText(tr("Undo"));
    m_menuRedoAction->setText(tr("Redo"));
    m_viewMenu->setTitle(tr("View"));
    m_selectAllAction->setText(tr("Select All"));
    m_fitViewAction->setText(tr("Fit View"));
    m_resetViewAction->setText(tr("Reset View"));
    m_aiMenu->setTitle(tr("AI"));
    m_aiSettingsAction->setText(tr("AI Settings..."));
    m_generateAction->setText(tr("Generate selected node"));
    m_cancelGenerationAction->setText(tr("Cancel generation"));
    updateGenerationUi();
    m_languageMenu->setTitle(tr("Language"));
    m_englishLanguageAction->setText(tr("English"));
    m_chineseLanguageAction->setText(tr("Chinese"));

    m_propertiesDock->setWindowTitle(tr("Properties"));
    m_propertiesDockAction->setText(tr("Properties"));
    m_nodePropertiesEditor->retranslateUi();
    m_applyPropertiesButton->setText(tr("Apply"));

    m_buildDock->setWindowTitle(tr("Build and export"));
    m_buildDockAction->setText(tr("Build and export"));
    m_resetLayoutAction->setText(tr("Reset Layout"));
    m_workspacePathEdit->setToolTip(tr("Workspace root containing generated-project"));
    m_configureArgumentsEdit->setPlaceholderText(
        tr("For example: -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.x/mingw_64"));
    setFormLabel(m_buildForm, m_workspacePathEdit, tr("Workspace root"));
    setFormLabel(m_buildForm, m_buildDirectoryEdit, tr("Build directory"));
    setFormLabel(m_buildForm, m_exportTargetEdit, tr("Empty export directory"));
    setFormLabel(m_buildForm, m_cmakeExecutableEdit, tr("CMake executable"));
    setFormLabel(m_buildForm, m_configureArgumentsEdit, tr("Configure arguments"));
    m_buildProjectButton->setText(tr("Build"));
    m_exportProjectButton->setText(tr("Export"));
    m_toolbarBuildAction->setText(tr("Build"));
    m_toolbarExportAction->setText(tr("Export"));
    m_buildLog->setPlaceholderText(tr("Configure, build, and export output appears here."));

    statusBar()->clearMessage();
    for (const auto &node : m_document.nodes) {
        if (auto *item = m_scene->nodeItem(node.id))
            item->refreshToolTip();
    }
    m_scene->update();
}

void MainWindow::setBuildToolConfiguration(const QString &cmakeExecutable,
                                           const QStringList &configureArguments,
                                           const QStringList &buildArguments)
{
    m_cmakeExecutableEdit->setText(cmakeExecutable);
    m_configureArgumentsEdit->setText(encodeCommandArguments(configureArguments));
    m_buildArguments = buildArguments;
}

void MainWindow::startGeneration()
{
    if (!m_generateAction->isEnabled())
        return;
    m_generationNodeId = selectedNodeId();
    // Capture before start(): injected clients may complete synchronously.
    m_generationSnapshot = m_document;
    QSettings settings;
    m_generationController->start(m_generationSnapshot, m_generationNodeId,
                                 m_workspacePathEdit->text().trimmed(),
                                 AiProviderSettings::load(settings));
    updateGenerationUi();
}

void MainWindow::invalidateGenerationContext()
{
    m_generationController->invalidateContext(m_document, m_workspacePathEdit->text().trimmed());
    if (m_reviewDialog)
        m_reviewDialog->invalidateContext();
    updateGenerationUi();
}

void MainWindow::updateGenerationUi()
{
    if (!m_generationController)
        return;
    const auto state = m_generationController->state();
    bool generatable = false;
    const QString id = selectedNodeId();
    if (m_scene->selectedItems().size() == 1) {
        for (const auto &node : m_document.nodes) {
            if (node.id == id)
                generatable = node.type == NodeType::LogicModule || node.type == NodeType::UiPage
                    || node.type == NodeType::Decision;
        }
    }
    m_generateAction->setEnabled(generatable && state != GenerationController::State::Generating
                                && !m_reviewDialog);
    m_cancelGenerationAction->setEnabled(state == GenerationController::State::Generating);
    QString status;
    switch (state) {
    case GenerationController::State::Idle: status = tr("Ready to generate"); break;
    case GenerationController::State::Generating: status = tr("Generating"); break;
    case GenerationController::State::Success: status = tr("Success — review candidates"); break;
    case GenerationController::State::Failed:
        status = tr("Failed: %1").arg(m_generationController->errorMessage()); break;
    case GenerationController::State::Cancelled: status = tr("Cancelled"); break;
    }
    m_generationStatus->setText(m_generationNodeId.isEmpty() ? status
        : tr("%1: %2").arg(m_generationNodeId, status));
}

void MainWindow::showValidationDiagnostics()
{
    const QVector<BlueprintDiagnostic> diagnostics = m_generationController->diagnostics();
    if (m_generationController->state() != GenerationController::State::Failed
        || diagnostics.isEmpty()) {
        return;
    }
    // A blueprint structure error is not an AI failure, so it gets its own report
    // instead of only the generic status bar message.
    ValidationDiagnosticsDialog dialog(diagnostics, this);
    dialog.exec();
}

void MainWindow::startBuild()
{
    BuildRequest request;
    const QString workspace = m_workspacePathEdit->text().trimmed();
    request.buildDirectory = m_buildDirectoryEdit->text().trimmed();
    if (workspace.isEmpty() || request.buildDirectory.isEmpty()) {
        appendBuildLog(tr("Build request rejected: workspace and build paths must not be empty.\n"));
        return;
    }
    request.sourceDirectory = QDir(workspace)
                                  .filePath(QStringLiteral("generated-project"));
    request.cmakeExecutable = m_cmakeExecutableEdit->text().trimmed();
    request.configureArguments = QProcess::splitCommand(m_configureArgumentsEdit->text());
    request.buildArguments = m_buildArguments;
    QString error;
    appendBuildLog(tr("Starting configure for %1\n").arg(request.sourceDirectory));
    m_buildProjectButton->setEnabled(false);
    m_toolbarBuildAction->setEnabled(false);
    if (!m_buildService->start(request, &error)) {
        m_buildProjectButton->setEnabled(true);
        m_toolbarBuildAction->setEnabled(true);
        appendBuildLog(tr("Build request rejected: %1\n").arg(error));
        return;
    }
}

void MainWindow::exportProject()
{
    QString error;
    if (ProjectExporter::exportProject(m_workspacePathEdit->text().trimmed(),
                                       m_exportTargetEdit->text().trimmed(), &error)) {
        appendBuildLog(tr("Export finished successfully.\n"));
        return;
    }
    appendBuildLog(tr("Export failed: %1\n").arg(error));
}

void MainWindow::resetWindowLayout()
{
    for (QDockWidget *dock : {m_propertiesDock, m_buildDock}) {
        dock->setFloating(false);
        removeDockWidget(dock);
    }
    addDockWidget(Qt::RightDockWidgetArea, m_propertiesDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_buildDock);
    m_propertiesDock->show();
    m_buildDock->show();
    resizeDocks({m_propertiesDock}, {340}, Qt::Horizontal);
    resizeDocks({m_buildDock}, {260}, Qt::Vertical);
}

void MainWindow::appendBuildLog(const QString &text)
{
    m_buildLog->moveCursor(QTextCursor::End);
    m_buildLog->insertPlainText(text);
    m_buildLog->moveCursor(QTextCursor::End);
}

void MainWindow::showCanvasContextMenu(QGraphicsItem *target, const QPoint &screenPosition,
                                       const QPointF &scenePosition)
{
    // The scene already made the clicked object the current one, so every entry can keep
    // reusing the shared actions: Delete acts on the selection, Edit on the clicked node.
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("canvasContextMenu"));
    if (auto *node = dynamic_cast<NodeItem *>(target)) {
        const QString nodeId = node->nodeId();
        QAction *editAction = menu.addAction(tr("Edit node"));
        editAction->setObjectName(QStringLiteral("contextEditNodeAction"));
        connect(editAction, &QAction::triggered, this, [this, nodeId] { editNodeFromCanvas(nodeId); });
        menu.addAction(m_generateAction);
        menu.addSeparator();
        menu.addAction(m_deleteAction);
    } else if (dynamic_cast<EdgeItem *>(target)) {
        menu.addAction(m_deleteAction);
    } else {
        QMenu *addNodeMenu = menu.addMenu(tr("Add node"));
        addNodeMenu->setObjectName(QStringLiteral("canvasAddNodeMenu"));
        for (const NodeType type : blueprintNodeTypes()) {
            QAction *action = addNodeMenu->addAction(nodeTypeDisplayName(type));
            connect(action, &QAction::triggered, this,
                    [this, type, scenePosition] { addNodeOfTypeAt(type, scenePosition); });
        }
        menu.addSeparator();
        menu.addAction(m_selectAllAction);
        menu.addAction(m_fitViewAction);
        menu.addAction(m_resetViewAction);
    }
    menu.exec(screenPosition);
}

void MainWindow::editNodeFromCanvas(const QString &nodeId)
{
    const auto iterator = std::find_if(m_document.nodes.cbegin(), m_document.nodes.cend(),
                                       [&nodeId](const BlueprintNode &node) {
                                           return node.id == nodeId;
                                       });
    if (iterator == m_document.nodes.cend()) {
        return;
    }

    m_scene->clearSelection();
    if (NodeItem *item = m_scene->nodeItem(nodeId)) {
        item->setSelected(true);
    }
    NodeEditDialog dialog(*iterator, this);
    if (dialog.exec() == QDialog::Accepted) {
        m_scene->editNode(nodeId, dialog.node());
        statusBar()->clearMessage();
    }
}

void MainWindow::applyProperties()
{
    const QString id = selectedNodeId();
    if (!id.isEmpty()) {
        BlueprintNode updated;
        for (const BlueprintNode &node : m_document.nodes) {
            if (node.id == id) {
                updated = node;
                break;
            }
        }
        m_nodePropertiesEditor->applyTo(&updated);
        m_scene->editNode(id, updated);
        statusBar()->clearMessage();
    }
}

void MainWindow::cancelConnection()
{
    m_scene->cancelConnection();
    statusBar()->clearMessage();
}

void MainWindow::updatePropertyEditor()
{
    const QString id = selectedNodeId();
    const bool editable = !id.isEmpty();
    m_nodePropertiesEditor->setEditorEnabled(editable);
    m_applyPropertiesButton->setEnabled(editable);
    if (!editable) {
        m_nodePropertiesEditor->clear();
        return;
    }

    for (const BlueprintNode &node : m_document.nodes) {
        if (node.id == id) {
            m_nodePropertiesEditor->setNode(node);
            return;
        }
    }
}

QString MainWindow::selectedNodeId() const
{
    QString selectedId;
    for (QGraphicsItem *item : m_scene->selectedItems()) {
        if (auto *node = dynamic_cast<NodeItem *>(item)) {
            if (!selectedId.isEmpty()) {
                return {};
            }
            selectedId = node->nodeId();
        }
    }
    return selectedId;
}
