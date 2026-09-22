#include "ui/node_type_display.h"

#include <QObject>

QString nodeTypeDisplayName(NodeType type)
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
