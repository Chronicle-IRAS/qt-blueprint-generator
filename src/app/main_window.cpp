#include "app/main_window.h"

#include "editor/blueprint_scene.h"
#include "editor/node_item.h"

#include <QAction>
#include <QDockWidget>
#include <QFormLayout>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

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

    auto *toolbar = addToolBar(tr("Blueprint"));
    auto *addButton = new QToolButton(toolbar);
    addButton->setText(tr("Add node"));
    addButton->setPopupMode(QToolButton::InstantPopup);
    auto *addMenu = new QMenu(addButton);
    const QVector<NodeType> nodeTypes{
        NodeType::Start,
        NodeType::End,
        NodeType::UiPage,
        NodeType::LogicModule,
        NodeType::Decision,
        NodeType::ExternalCode,
    };
    for (const NodeType type : nodeTypes) {
        QAction *action = addMenu->addAction(nodeTypeName(type));
        action->setObjectName(QStringLiteral("add%1NodeAction").arg(nodeTypeName(type).remove(' ')));
        connect(action, &QAction::triggered, this, [this, type] { addNodeOfType(type); });
    }
    addButton->setMenu(addMenu);
    toolbar->addWidget(addButton);
    m_connectionLabelEdit = new QLineEdit(toolbar);
    m_connectionLabelEdit->setObjectName(QStringLiteral("connectionLabelEdit"));
    m_connectionLabelEdit->setPlaceholderText(tr("Edge label (optional)"));
    m_connectionLabelEdit->setToolTip(tr("Label for the next source-to-target connection"));
    m_connectionLabelEdit->setMaximumWidth(220);
    toolbar->addWidget(m_connectionLabelEdit);
    QAction *deleteAction = toolbar->addAction(tr("Delete"));
    QAction *connectAction = toolbar->addAction(tr("Connect: choose source then target"));
    connectAction->setObjectName(QStringLiteral("beginConnectionAction"));
    auto *cancelAction = new QAction(tr("Cancel connection"), this);
    cancelAction->setObjectName(QStringLiteral("cancelConnectionAction"));
    cancelAction->setShortcut(QKeySequence(Qt::Key_Escape));
    cancelAction->setShortcutContext(Qt::WindowShortcut);
    toolbar->addAction(cancelAction);
    toolbar->addSeparator();
    toolbar->addAction(m_scene->undoStack()->createUndoAction(this, tr("Undo")));
    toolbar->addAction(m_scene->undoStack()->createRedoAction(this, tr("Redo")));

    QMenu *editMenu = menuBar()->addMenu(tr("Edit"));
    editMenu->addAction(m_scene->undoStack()->createUndoAction(this, tr("Undo")));
    editMenu->addAction(m_scene->undoStack()->createRedoAction(this, tr("Redo")));

    auto *properties = new QDockWidget(tr("Properties"), this);
    auto *propertyWidget = new QWidget(properties);
    auto *propertyLayout = new QVBoxLayout(propertyWidget);
    auto *form = new QFormLayout;
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
    form->addRow(tr("Name"), m_nameEdit);
    form->addRow(tr("Description"), m_descriptionEdit);
    form->addRow(tr("Inputs (JSON)"), m_inputsEdit);
    form->addRow(tr("Outputs (JSON)"), m_outputsEdit);
    form->addRow(tr("Constraints (JSON)"), m_constraintsEdit);
    form->addRow(tr("Acceptance criteria (JSON)"), m_acceptanceCriteriaEdit);
    propertyLayout->addLayout(form);
    m_applyPropertiesButton = new QPushButton(tr("Apply"), propertyWidget);
    m_applyPropertiesButton->setObjectName(QStringLiteral("applyNodePropertiesButton"));
    propertyLayout->addWidget(m_applyPropertiesButton);
    propertyLayout->addStretch();
    properties->setWidget(propertyWidget);
    addDockWidget(Qt::RightDockWidgetArea, properties);

    connect(deleteAction, &QAction::triggered, this, [this] { deleteSelection(); });
    connect(connectAction, &QAction::triggered, this, [this] {
        m_scene->beginConnection(m_connectionLabelEdit->text());
        statusBar()->showMessage(tr("Choose source node, then target node"));
    });
    connect(cancelAction, &QAction::triggered, this, [this] { cancelConnection(); });
    connect(m_applyPropertiesButton, &QPushButton::clicked, this, [this] { applyProperties(); });
    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this] { updatePropertyEditor(); });
    m_scene->setSemanticChangeHandler([this] { updatePropertyEditor(); });
    updatePropertyEditor();
}

MainWindow::~MainWindow()
{
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
        updated.name = m_nameEdit->text();
        updated.description = m_descriptionEdit->toPlainText();
        if (!parsePorts(m_inputsEdit->toPlainText(), &updated.inputs)
            || !parsePorts(m_outputsEdit->toPlainText(), &updated.outputs)
            || !parseStrings(m_constraintsEdit->toPlainText(), &updated.constraints)
            || !parseStrings(m_acceptanceCriteriaEdit->toPlainText(), &updated.acceptanceCriteria)) {
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
