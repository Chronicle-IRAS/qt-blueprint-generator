#include "app/blueprint_v2_view.h"
#include "ui/theme.h"
#include <QEvent>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QVBoxLayout>
#include <QShowEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QMenu>
#include <QContextMenuEvent>
#include <QPushButton>
#include <QHBoxLayout>

namespace {
class ReadOnlyGraphicsView final : public QGraphicsView {
public:
    using QGraphicsView::QGraphicsView;
protected:
    void wheelEvent(QWheelEvent *event) override {
        if (event->modifiers() & Qt::ControlModifier) {
            const qreal factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
            const qreal nextScale = transform().m11() * factor;
            if (nextScale >= 0.1 && nextScale <= 4.0) scale(factor, factor);
            event->accept();
        } else QGraphicsView::wheelEvent(event);
    }
    void contextMenuEvent(QContextMenuEvent *event) override {
        QMenu menu(this);
        auto *fit = menu.addAction(BlueprintV2View::tr("Fit View"));
        auto *reset = menu.addAction(BlueprintV2View::tr("Reset View"));
        auto *select = menu.addAction(BlueprintV2View::tr("Select All"));
        const auto *chosen = menu.exec(event->globalPos());
        if (chosen == fit) fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
        if (chosen == reset) resetTransform();
        if (chosen == select) for (auto *item : scene()->items()) if (item->flags() & QGraphicsItem::ItemIsSelectable) item->setSelected(true);
    }
};
}

BlueprintV2View::BlueprintV2View(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    m_banner = new QLabel(this); m_banner->setObjectName("v2ReadOnlyBanner");
    m_banner->setWordWrap(true);
    m_banner->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_banner->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *header=new QHBoxLayout;header->addWidget(m_banner,1);
    m_editButton=new QPushButton(tr("Edit v2 values…"),this);m_editButton->setObjectName("v2EditValuesButton");header->addWidget(m_editButton);layout->addLayout(header);
    connect(m_editButton,&QPushButton::clicked,this,&BlueprintV2View::editRequested);
    m_status=new QLabel(this);m_status->setObjectName("v2OperationStatus");m_status->setWordWrap(true);m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);m_status->hide();layout->addWidget(m_status);
    auto *split = new QSplitter(this); layout->addWidget(split, 1);
    m_scene = new QGraphicsScene(this);
    m_view = new ReadOnlyGraphicsView(m_scene, split); m_view->setObjectName("v2GraphicsView");
    m_view->setRenderHint(QPainter::Antialiasing); m_view->setDragMode(QGraphicsView::RubberBandDrag);
    auto *details = new QSplitter(Qt::Vertical, split);
    m_properties = new QPlainTextEdit(details); m_properties->setReadOnly(true); m_properties->setObjectName("v2Properties");
    m_diagnostics = new QPlainTextEdit(details); m_diagnostics->setReadOnly(true); m_diagnostics->setObjectName("v2Diagnostics");
    split->setStretchFactor(0, 3); split->setStretchFactor(1, 2);
    split->setSizes({660, 400});
    connect(m_scene, &QGraphicsScene::selectionChanged, this, &BlueprintV2View::showSelection);
}
void BlueprintV2View::setReply(const QJsonObject &reply) { m_reply = reply; refreshPresentation(); }
int BlueprintV2View::selectedSourceIndex() const {
    for(auto *item:m_scene->selectedItems())if(item->data(0).isValid())return item->data(0).toInt();
    return -1;
}
void BlueprintV2View::setEditState(bool writable,bool busy,const QString &reason) {m_writable=writable;m_busy=busy;m_readOnlyReason=reason;refreshPresentation();}
void BlueprintV2View::setOperationStatus(const QString &text) {m_status->setText(text);m_status->setVisible(!text.isEmpty());}
BlueprintV2View::~BlueprintV2View() { disconnect(m_scene, nullptr, this, nullptr); }
void BlueprintV2View::changeEvent(QEvent *event) { QWidget::changeEvent(event); if (event->type() == QEvent::LanguageChange) refreshPresentation(); }
void BlueprintV2View::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    m_view->fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
}
void BlueprintV2View::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    m_view->fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
}
void BlueprintV2View::showSelection() {
    const auto nodes = m_reply["nodes"].toArray();
    for (auto *item : m_scene->selectedItems()) if (item->data(0).isValid()) {
        int index = item->data(0).toInt();
        m_properties->setPlainText(tr("Node source index: %1\nRead-only snapshot\n").arg(index) + QString::fromUtf8(QJsonDocument(nodes[index].toObject()).toJson(QJsonDocument::Indented)));
        return;
    }
    m_properties->setPlainText(tr("Select a node to inspect its full properties and port snapshot."));
}
void BlueprintV2View::refreshPresentation() {
    int selected = -1;
    for (auto *item : m_scene->selectedItems()) if (item->data(0).isValid()) selected = item->data(0).toInt();
    m_scene->clear();
    const auto &colors = EditorTheme::colors(); m_scene->setBackgroundBrush(colors.canvas);
    m_banner->setText(m_writable
        ? tr("Blueprint v2 — Restricted editing · Generation, build and export unavailable")
        : tr("Blueprint v2 — Read-only") + (m_readOnlyReason.isEmpty() ? QString() : " · " + m_readOnlyReason));
    m_banner->setToolTip(m_writable
        ? tr("Blueprint v2 — Restricted editing · Generation, build and export unavailable")
        : tr("Blueprint v2 — Read-only · Generation, saving, build and export unavailable"));
    m_editButton->setText(tr("Edit v2 values…"));m_editButton->setEnabled(m_writable&&!m_busy);
    const auto nodes = m_reply["nodes"].toArray();
    QVector<QGraphicsRectItem *> items;
    qreal rowY = 0, rowHeight = 0;
    for (int i = 0; i < nodes.size(); ++i) {
        if (i && i % 2 == 0) { rowY += rowHeight + 100; rowHeight = 0; }
        const auto node = nodes[i].toObject(); const auto data = node["data"].toObject();
        const auto ports = data["ports"].toArray();
        auto *item = m_scene->addRect(QRectF(0, 0, 330, 118 + ports.size() * 40), QPen(colors.nodeBorder, 2), colors.nodeBody);
        item->setPos((i % 2) * 380, rowY); item->setFlag(QGraphicsItem::ItemIsSelectable); item->setData(0, i);
        rowHeight = qMax(rowHeight, item->rect().height());
        QString text = tr("#%1 · %2 · %3").arg(i).arg(node["nodeId"].toString(), node["kind"] == "known" ? tr("KnownNode") : tr("UnknownNode"));
        text += "\n" + node.value("title").toString(node["typeId"].toString()) + "\n" + node["unitId"].toString() + " · " + node["nodeDefinitionVersion"].toString();
        for (const auto &port : ports) { const auto p = port.toObject(); text += "\n" + p["direction"].toString() + " · " + p["id"].toString() + " · " + p["name"].toString() + "\n  " + p["kind"].toString() + " · " + p["typeId"].toString(); }
        auto *label = new QGraphicsTextItem(text, item); label->setDefaultTextColor(colors.text); label->setTextWidth(314); label->setPos(6, 6);
        if (selected == i) item->setSelected(true);
        items.push_back(item);
    }
    QStringList relationships;
    const auto endpoint = [&](const QJsonObject &ep, QPointF &point) {
        int index = -1;
        for (int i = 0; i < nodes.size(); ++i) {
            const auto n = nodes[i].toObject();
            if (n["nodeId"] == ep["nodeId"]) { if (index != -1) return false; index = i; }
        }
        if (index < 0) return false;
        const auto n = nodes[index].toObject();
        if (ep.contains("unitId") && ep["unitId"] != n["unitId"]) return false;
        const auto ports = n["data"].toObject()["ports"].toArray(); int count = 0, portIndex = 0;
        for (int p = 0; p < ports.size(); ++p) if (ports[p].toObject()["id"] == ep["portId"]) { ++count; portIndex = p; }
        if (count != 1) return false;
        point = items[index]->pos() + QPointF(ports[portIndex].toObject()["direction"] == "out" ? 330 : 0, 118 + portIndex * 40); return true;
    };
    const auto document = m_reply["document"].toObject();
    for (auto collection : {"edges", "channels"}) for (const auto &value : document[collection].toArray()) {
        const auto relation = value.toObject(); const bool channel = QString::fromLatin1(collection) == "channels";
        QPointF source, target;
        bool resolved = endpoint(relation["source"].toObject(), source) && endpoint(relation["target"].toObject(), target);
        const QString label = (channel ? tr("Declared channel (metadata only)") : tr("Edge")) + " · " + relation["id"].toString();
        relationships << label + (resolved ? tr(" — displayed") : tr(" — unresolved: ambiguous or missing node/port endpoint"));
        relationships << QString::fromUtf8(QJsonDocument(relation).toJson(QJsonDocument::Indented));
        if (resolved) {
            QPen pen(channel ? colors.accent : colors.edge, 2); if (channel) pen.setStyle(Qt::DashLine);
            auto *line = m_scene->addLine(QLineF(source, target), pen); line->setZValue(-1);
            auto *text = m_scene->addText(label); text->setDefaultTextColor(colors.textMuted); text->setTextWidth(250); text->setPos((source + target) / 2);
        }
    }
    QString diagnostics = tr("Diagnostics") + "\n";
    const auto ds = m_reply["diagnostics"].toArray();
    if (ds.isEmpty()) diagnostics += tr("No diagnostics.") + "\n";
    for (const auto &value : ds) diagnostics += QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Indented));
    diagnostics += "\n" + tr("Relationships") + "\n" + relationships.join('\n');
    m_diagnostics->setPlainText(diagnostics);
    showSelection(); m_scene->setSceneRect(m_scene->itemsBoundingRect().adjusted(-40, -40, 40, 40));
    m_view->fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
}
