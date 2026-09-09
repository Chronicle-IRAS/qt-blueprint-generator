#include "app/main_window.h"

#include "editor/blueprint_scene.h"
#include "editor/node_item.h"
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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <utility>

namespace {

QString nodeTypeName(NodeType type)
{
    switch (type) {
    case NodeType::Start:
        return QObject::tr("Start");
    case NodeType::End:
        return QObject::tr("End");
    case NodeType::UiPage:
        return QObject::tr("UI Page");
    case NodeType::LogicModule:
        return QObject::tr("Logic Module");
    case NodeType::Decision:
        return QObject::tr("Decision");
    case NodeType::ExternalCode:
        return QObject::tr("External Code");
    }
    return {};
}

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

QString portsToText(const QVector<PortSpec> &ports)
{
    QJsonArray array;
    for (const PortSpec &port : ports) {
        array.append(QJsonObject{{QStringLiteral("name"), port.name},
                                 {QStringLiteral("type"), port.type},
                                 {QStringLiteral("description"), port.description}});
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Indented));
}

QString stringsToText(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values) {
        array.append(value);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Indented));
}

bool parsePorts(const QString &text, QVector<PortSpec> *ports)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        ports->clear();
        return true;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) {
        return false;
    }
    QVector<PortSpec> parsed;
    for (const QJsonValue &value : document.array()) {
        if (!value.isObject()) {
            return false;
        }
        const QJsonObject object = value.toObject();
        if (!object.value(QStringLiteral("name")).isString()
            || !object.value(QStringLiteral("type")).isString()
            || !object.value(QStringLiteral("description")).isString()) {
            return false;
        }
        parsed.append({object.value(QStringLiteral("name")).toString(),
                       object.value(QStringLiteral("type")).toString(),
                       object.value(QStringLiteral("description")).toString()});
    }
    *ports = parsed;
    return true;
}

bool parseStrings(const QString &text, QStringList *values)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        values->clear();
        return true;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) {
        return false;
    }
    QStringList parsed;
    for (const QJsonValue &value : document.array()) {
        if (!value.isString()) {
            return false;
        }
        parsed.append(value.toString());
    }
    *values = parsed;
    return true;
}

bool updateNodeFromEditors(BlueprintNode *node,
                           const QLineEdit *nameEdit,
                           const QPlainTextEdit *descriptionEdit,
                           const QPlainTextEdit *inputsEdit,
                           const QPlainTextEdit *outputsEdit,
                           const QPlainTextEdit *constraintsEdit,
                           const QPlainTextEdit *acceptanceCriteriaEdit)
{
    BlueprintNode updated = *node;
    updated.name = nameEdit->text();
    updated.description = descriptionEdit->toPlainText();
    if (!parsePorts(inputsEdit->toPlainText(), &updated.inputs)
        || !parsePorts(outputsEdit->toPlainText(), &updated.outputs)
        || !parseStrings(constraintsEdit->toPlainText(), &updated.constraints)
        || !parseStrings(acceptanceCriteriaEdit->toPlainText(),
                         &updated.acceptanceCriteria)) {
        return false;
    }
    *node = std::move(updated);
    return true;
}

void setFormLabel(QFormLayout *form, QWidget *field, const QString &text)
{
    if (auto *label = qobject_cast<QLabel *>(form->labelForField(field))) {
        label->setText(text);
    }
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
        auto *form = new QFormLayout;
        m_nameEdit = new QLineEdit(node.name, this);
        m_nameEdit->setObjectName(QStringLiteral("directNodeNameEdit"));
        m_descriptionEdit = new QPlainTextEdit(node.description, this);
        m_descriptionEdit->setObjectName(QStringLiteral("directNodeDescriptionEdit"));
        m_descriptionEdit->setTabChangesFocus(false);
        m_descriptionEdit->setMaximumHeight(100);
        m_inputsEdit = createJsonEditor(QStringLiteral("directNodeInputsEdit"),
                                        portsToText(node.inputs));
        m_outputsEdit = createJsonEditor(QStringLiteral("directNodeOutputsEdit"),
                                         portsToText(node.outputs));
        m_constraintsEdit = createJsonEditor(QStringLiteral("directNodeConstraintsEdit"),
                                             stringsToText(node.constraints));
        m_acceptanceCriteriaEdit = createJsonEditor(
            QStringLiteral("directNodeAcceptanceCriteriaEdit"),
            stringsToText(node.acceptanceCriteria));

        form->addRow(MainWindow::tr("Name"), m_nameEdit);
        form->addRow(MainWindow::tr("Description"), m_descriptionEdit);
        form->addRow(MainWindow::tr("Inputs (JSON)"), m_inputsEdit);
        form->addRow(MainWindow::tr("Outputs (JSON)"), m_outputsEdit);
        form->addRow(MainWindow::tr("Constraints (JSON)"), m_constraintsEdit);
        form->addRow(MainWindow::tr("Acceptance criteria (JSON)"),
                     m_acceptanceCriteriaEdit);
        layout->addLayout(form);

        m_validationMessage = new QLabel(this);
        m_validationMessage->setObjectName(QStringLiteral("nodeEditValidationMessage"));
        m_validationMessage->setWordWrap(true);
        layout->addWidget(m_validationMessage);

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
            if (!updateNodeFromEditors(&updated, m_nameEdit, m_descriptionEdit, m_inputsEdit,
                                       m_outputsEdit, m_constraintsEdit,
                                       m_acceptanceCriteriaEdit)) {
                m_validationMessage->setText(
                    MainWindow::tr("Properties use valid JSON arrays for ports and lists"));
                return;
            }
            m_node = std::move(updated);
            accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

    const BlueprintNode &node() const
    {
        return m_node;
    }

private:
    QPlainTextEdit *createJsonEditor(const QString &objectName, const QString &text)
    {
        auto *editor = new QPlainTextEdit(text, this);
        editor->setObjectName(objectName);
        editor->setTabChangesFocus(false);
        editor->setMaximumHeight(100);
        return editor;
    }

    BlueprintNode m_node;
    QLineEdit *m_nameEdit = nullptr;
    QPlainTextEdit *m_descriptionEdit = nullptr;
    QPlainTextEdit *m_inputsEdit = nullptr;
    QPlainTextEdit *m_outputsEdit = nullptr;
    QPlainTextEdit *m_constraintsEdit = nullptr;
    QPlainTextEdit *m_acceptanceCriteriaEdit = nullptr;
    QLabel *m_validationMessage = nullptr;
};

class BlueprintView final : public QGraphicsView
{
public:
    explicit BlueprintView(QGraphicsScene *scene, QWidget *parent = nullptr)
        : QGraphicsView(scene, parent)
    {
    }

protected:
    void wheelEvent(QWheelEvent *event) override
    {
        constexpr qreal MinZoom = 0.25;
        constexpr qreal MaxZoom = 3.0;
        const qreal factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
        const qreal nextZoom = transform().m11() * factor;
        if (nextZoom >= MinZoom && nextZoom <= MaxZoom) {
            scale(factor, factor);
        }
        event->accept();
    }
};

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
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
    const QVector<NodeType> nodeTypes{
        NodeType::Start,
        NodeType::End,
        NodeType::UiPage,
        NodeType::LogicModule,
        NodeType::Decision,
        NodeType::ExternalCode,
    };
    for (const NodeType type : nodeTypes) {
        QAction *action = m_addNodeMenu->addAction(nodeTypeName(type));
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
    m_propertyForm = new QFormLayout;
    m_nameEdit = new QLineEdit(propertyWidget);
    m_nameEdit->setObjectName(QStringLiteral("nodeNameEdit"));
    m_descriptionEdit = new QPlainTextEdit(propertyWidget);
    m_descriptionEdit->setObjectName(QStringLiteral("nodeDescriptionEdit"));
    m_descriptionEdit->setPlaceholderText(tr("Description"));
    m_inputsEdit = new QPlainTextEdit(propertyWidget);
    m_inputsEdit->setObjectName(QStringLiteral("nodeInputsEdit"));
    m_outputsEdit = new QPlainTextEdit(propertyWidget);
    m_outputsEdit->setObjectName(QStringLiteral("nodeOutputsEdit"));
    m_constraintsEdit = new QPlainTextEdit(propertyWidget);
    m_constraintsEdit->setObjectName(QStringLiteral("nodeConstraintsEdit"));
    m_acceptanceCriteriaEdit = new QPlainTextEdit(propertyWidget);
    m_acceptanceCriteriaEdit->setObjectName(QStringLiteral("nodeAcceptanceCriteriaEdit"));
    for (QPlainTextEdit *editor : {m_inputsEdit, m_outputsEdit, m_constraintsEdit, m_acceptanceCriteriaEdit}) {
        editor->setTabChangesFocus(false);
        editor->setMaximumHeight(90);
    }
    m_propertyForm->addRow(tr("Name"), m_nameEdit);
    m_propertyForm->addRow(tr("Description"), m_descriptionEdit);
    m_propertyForm->addRow(tr("Inputs (JSON)"), m_inputsEdit);
    m_propertyForm->addRow(tr("Outputs (JSON)"), m_outputsEdit);
    m_propertyForm->addRow(tr("Constraints (JSON)"), m_constraintsEdit);
    m_propertyForm->addRow(tr("Acceptance criteria (JSON)"), m_acceptanceCriteriaEdit);
    propertyLayout->addLayout(m_propertyForm);
    m_applyPropertiesButton = new QPushButton(tr("Apply"), propertyWidget);
    m_applyPropertiesButton->setObjectName(QStringLiteral("applyNodePropertiesButton"));
    propertyLayout->addWidget(m_applyPropertiesButton);
    propertyLayout->addStretch();
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
    buildLayout->addLayout(m_buildForm);
    auto *buttons = new QHBoxLayout;
    m_buildProjectButton = new QPushButton(tr("Build"), buildWidget);
    m_buildProjectButton->setObjectName(QStringLiteral("buildProjectButton"));
    m_exportProjectButton = new QPushButton(tr("Export"), buildWidget);
    m_exportProjectButton->setObjectName(QStringLiteral("exportProjectButton"));
    buttons->addWidget(m_buildProjectButton);
    buttons->addWidget(m_exportProjectButton);
    buttons->addStretch();
    buildLayout->addLayout(buttons);
    m_buildLog = new QPlainTextEdit(buildWidget);
    m_buildLog->setObjectName(QStringLiteral("buildLog"));
    m_buildLog->setReadOnly(true);
    m_buildLog->setPlaceholderText(tr("Configure, build, and export output appears here."));
    m_buildLog->setMaximumBlockCount(10000);
    buildLayout->addWidget(m_buildLog);
    m_buildDock->setWidget(buildWidget);
    addDockWidget(Qt::BottomDockWidgetArea, m_buildDock);

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

    connect(m_deleteAction, &QAction::triggered, this, [this] { deleteSelection(); });
    connect(m_connectAction, &QAction::triggered, this, [this] {
        m_scene->beginConnection(m_connectionLabelEdit->text());
        statusBar()->showMessage(tr("Choose source node, then target node"));
    });
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
    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this] { updatePropertyEditor(); });
    m_scene->setSemanticChangeHandler([this] { updatePropertyEditor(); });
    updatePropertyEditor();

    const QString savedLanguage = QSettings().value(QStringLiteral("ui/language"),
                                                     QStringLiteral("en")).toString();
    if (!applyLanguage(savedLanguage, false)) {
        applyLanguage(QStringLiteral("en"), false);
    }
}

MainWindow::~MainWindow()
{
    qApp->removeTranslator(&m_translator);
    if (m_scene) {
        QObject::disconnect(m_scene, nullptr, this, nullptr);
        m_scene->setSemanticChangeHandler({});
    }
}

bool MainWindow::addNodeOfType(NodeType type)
{
    int number = 1;
    QString id;
    do {
        id = QStringLiteral("node-%1").arg(number++);
    } while (m_scene->nodeItem(id));

    BlueprintNode node;
    node.id = id;
    node.type = type;
    node.name = nodeTypeName(type);
    const QPointF center = m_view->mapToScene(m_view->viewport()->rect().center());
    const qsizetype ordinal = m_document.nodes.size();
    const QPointF position = center + QPointF((ordinal % 3) * 220.0, (ordinal / 3) * 140.0);
    const bool added = m_scene->addNode(node, position);
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
        action->setText(nodeTypeName(type));
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
    m_languageMenu->setTitle(tr("Language"));
    m_englishLanguageAction->setText(tr("English"));
    m_chineseLanguageAction->setText(tr("Chinese"));

    m_propertiesDock->setWindowTitle(tr("Properties"));
    m_propertiesDockAction->setText(tr("Properties"));
    m_descriptionEdit->setPlaceholderText(tr("Description"));
    setFormLabel(m_propertyForm, m_nameEdit, tr("Name"));
    setFormLabel(m_propertyForm, m_descriptionEdit, tr("Description"));
    setFormLabel(m_propertyForm, m_inputsEdit, tr("Inputs (JSON)"));
    setFormLabel(m_propertyForm, m_outputsEdit, tr("Outputs (JSON)"));
    setFormLabel(m_propertyForm, m_constraintsEdit, tr("Constraints (JSON)"));
    setFormLabel(m_propertyForm, m_acceptanceCriteriaEdit, tr("Acceptance criteria (JSON)"));
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
    resizeDocks({m_propertiesDock}, {300}, Qt::Horizontal);
    resizeDocks({m_buildDock}, {220}, Qt::Vertical);
}

void MainWindow::appendBuildLog(const QString &text)
{
    m_buildLog->moveCursor(QTextCursor::End);
    m_buildLog->insertPlainText(text);
    m_buildLog->moveCursor(QTextCursor::End);
}

void MainWindow::deleteSelection()
{
    QStringList ids;
    for (QGraphicsItem *item : m_scene->selectedItems()) {
        if (auto *node = dynamic_cast<NodeItem *>(item)) {
            ids.append(node->nodeId());
        }
    }
    for (const QString &id : ids) {
        m_scene->deleteNode(id);
    }
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
        if (!updateNodeFromEditors(&updated, m_nameEdit, m_descriptionEdit, m_inputsEdit,
                                   m_outputsEdit, m_constraintsEdit,
                                   m_acceptanceCriteriaEdit)) {
            statusBar()->showMessage(tr("Properties use valid JSON arrays for ports and lists"));
            return;
        }
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
    m_nameEdit->setEnabled(editable);
    m_descriptionEdit->setEnabled(editable);
    m_inputsEdit->setEnabled(editable);
    m_outputsEdit->setEnabled(editable);
    m_constraintsEdit->setEnabled(editable);
    m_acceptanceCriteriaEdit->setEnabled(editable);
    m_applyPropertiesButton->setEnabled(editable);
    if (!editable) {
        m_nameEdit->clear();
        m_descriptionEdit->clear();
        m_inputsEdit->clear();
        m_outputsEdit->clear();
        m_constraintsEdit->clear();
        m_acceptanceCriteriaEdit->clear();
        return;
    }

    for (const BlueprintNode &node : m_document.nodes) {
        if (node.id == id) {
            m_nameEdit->setText(node.name);
            m_descriptionEdit->setPlainText(node.description);
            m_inputsEdit->setPlainText(portsToText(node.inputs));
            m_outputsEdit->setPlainText(portsToText(node.outputs));
            m_constraintsEdit->setPlainText(stringsToText(node.constraints));
            m_acceptanceCriteriaEdit->setPlainText(stringsToText(node.acceptanceCriteria));
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
