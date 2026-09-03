#include "app/main_window.h"

#include "editor/blueprint_scene.h"
#include "editor/node_item.h"

#include <QAction>
#include <QDockWidget>
#include <QFormLayout>
#include <QGraphicsView>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {

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
    QAction *addAction = toolbar->addAction(tr("Add node"));
    QAction *deleteAction = toolbar->addAction(tr("Delete"));
    QAction *connectAction = toolbar->addAction(tr("Connect selected"));
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
    m_descriptionEdit = new QPlainTextEdit(propertyWidget);
    m_descriptionEdit->setPlaceholderText(tr("Description"));
    form->addRow(tr("Name"), m_nameEdit);
    form->addRow(tr("Description"), m_descriptionEdit);
    propertyLayout->addLayout(form);
    auto *applyButton = new QPushButton(tr("Apply"), propertyWidget);
    propertyLayout->addWidget(applyButton);
    propertyLayout->addStretch();
    properties->setWidget(propertyWidget);
    addDockWidget(Qt::RightDockWidgetArea, properties);

    connect(addAction, &QAction::triggered, this, [this] { addNode(); });
    connect(deleteAction, &QAction::triggered, this, [this] { deleteSelection(); });
    connect(connectAction, &QAction::triggered, this, [this] { connectSelection(); });
    connect(applyButton, &QPushButton::clicked, this, [this] { applyProperties(); });
    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this] { updatePropertyEditor(); });
    updatePropertyEditor();
}

void MainWindow::addNode()
{
    int number = 1;
    QString id;
    do {
        id = QStringLiteral("node-%1").arg(number++);
    } while (m_scene->nodeItem(id));

    BlueprintNode node;
    node.id = id;
    node.type = NodeType::LogicModule;
    node.name = tr("New node");
    const QPointF position = m_view->mapToScene(m_view->viewport()->rect().center());
    m_scene->addNode(node, position);
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

void MainWindow::connectSelection()
{
    QStringList ids;
    for (QGraphicsItem *item : m_scene->selectedItems()) {
        if (auto *node = dynamic_cast<NodeItem *>(item)) {
            ids.append(node->nodeId());
        }
    }
    if (ids.size() == 2) {
        m_scene->connectNodes(ids.at(0), ids.at(1));
    }
}

void MainWindow::applyProperties()
{
    const QString id = selectedNodeId();
    if (!id.isEmpty()) {
        m_scene->editNodeText(id, m_nameEdit->text(), m_descriptionEdit->toPlainText());
    }
}

void MainWindow::updatePropertyEditor()
{
    const QString id = selectedNodeId();
    const bool editable = !id.isEmpty();
    m_nameEdit->setEnabled(editable);
    m_descriptionEdit->setEnabled(editable);
    if (!editable) {
        m_nameEdit->clear();
        m_descriptionEdit->clear();
        return;
    }

    for (const BlueprintNode &node : m_document.nodes) {
        if (node.id == id) {
            m_nameEdit->setText(node.name);
            m_descriptionEdit->setPlainText(node.description);
            return;
        }
    }
}

QString MainWindow::selectedNodeId() const
{
    for (QGraphicsItem *item : m_scene->selectedItems()) {
        if (auto *node = dynamic_cast<NodeItem *>(item)) {
            return node->nodeId();
        }
    }
    return {};
}
