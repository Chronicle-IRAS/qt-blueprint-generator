#include "generation/ir_compiler.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

#include <algorithm>

namespace {

struct Neighbor
{
    const BlueprintEdge *edge = nullptr;
    const BlueprintNode *node = nullptr;
};

QString nodeTypeName(NodeType type)
{
    switch (type) {
    case NodeType::Start:
        return QStringLiteral("Start");
    case NodeType::End:
        return QStringLiteral("End");
    case NodeType::UiPage:
        return QStringLiteral("UiPage");
    case NodeType::LogicModule:
        return QStringLiteral("LogicModule");
    case NodeType::Decision:
        return QStringLiteral("Decision");
    case NodeType::ExternalCode:
        return QStringLiteral("ExternalCode");
    }
    return QStringLiteral("Unknown");
}

bool isGeneratable(NodeType type)
{
    return type == NodeType::UiPage || type == NodeType::LogicModule
        || type == NodeType::Decision;
}

QJsonArray portsToJson(const QVector<PortSpec> &ports)
{
    QJsonArray values;
    for (const PortSpec &port : ports) {
        values.append(QJsonObject{
            {QStringLiteral("name"), port.name},
            {QStringLiteral("type"), port.type},
            {QStringLiteral("description"), port.description},
        });
    }
    return values;
}

QJsonArray stringsToJson(const QStringList &strings)
{
    QJsonArray values;
    for (const QString &value : strings) {
        values.append(value);
    }
    return values;
}

QString projectNamespace(const BlueprintDocument &document)
{
    const QString source = document.projectName.isEmpty() ? document.projectId : document.projectName;
    QString result;
    result.reserve(source.size() + 1);
    for (const QChar character : source) {
        const ushort code = character.unicode();
        const bool asciiLetter = (code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z');
        const bool asciiDigit = code >= '0' && code <= '9';
        if (asciiLetter || asciiDigit) {
            result.append(character);
        } else if (!result.isEmpty() && result.back() != QLatin1Char('_')) {
            result.append(QLatin1Char('_'));
        }
    }
    while (result.endsWith(QLatin1Char('_'))) {
        result.chop(1);
    }
    if (result.isEmpty()) {
        result = QStringLiteral("GeneratedProject");
    }
    if (result.front().isDigit()) {
        result.prepend(QStringLiteral("Project_"));
    }

    static const QSet<QString> cppKeywords{
        QStringLiteral("alignas"),      QStringLiteral("alignof"),
        QStringLiteral("and"),               QStringLiteral("and_eq"),
        QStringLiteral("asm"),          QStringLiteral("auto"),
        QStringLiteral("bitand"),       QStringLiteral("bitor"),
        QStringLiteral("bool"),         QStringLiteral("break"),
        QStringLiteral("case"),         QStringLiteral("catch"),
        QStringLiteral("char"),         QStringLiteral("char16_t"),
        QStringLiteral("char32_t"),     QStringLiteral("class"),
        QStringLiteral("compl"),        QStringLiteral("concept"),
        QStringLiteral("const"),        QStringLiteral("constexpr"),
        QStringLiteral("const_cast"),   QStringLiteral("continue"),
        QStringLiteral("co_await"),     QStringLiteral("co_return"),
        QStringLiteral("co_yield"),     QStringLiteral("decltype"),
        QStringLiteral("default"),      QStringLiteral("delete"),
        QStringLiteral("do"),           QStringLiteral("double"),
        QStringLiteral("dynamic_cast"), QStringLiteral("else"),
        QStringLiteral("enum"),         QStringLiteral("explicit"),
        QStringLiteral("export"),       QStringLiteral("extern"),
        QStringLiteral("false"),        QStringLiteral("float"),
        QStringLiteral("for"),          QStringLiteral("friend"),
        QStringLiteral("goto"),         QStringLiteral("if"),
        QStringLiteral("import"),       QStringLiteral("inline"),
        QStringLiteral("int"),          QStringLiteral("long"),
        QStringLiteral("module"),       QStringLiteral("mutable"),
        QStringLiteral("namespace"),    QStringLiteral("new"),
        QStringLiteral("noexcept"),     QStringLiteral("not"),
        QStringLiteral("not_eq"),       QStringLiteral("nullptr"),
        QStringLiteral("operator"),     QStringLiteral("or"),
        QStringLiteral("or_eq"),        QStringLiteral("private"),
        QStringLiteral("protected"),    QStringLiteral("public"),
        QStringLiteral("register"),     QStringLiteral("reinterpret_cast"),
        QStringLiteral("requires"),     QStringLiteral("return"),
        QStringLiteral("short"),        QStringLiteral("signed"),
        QStringLiteral("sizeof"),       QStringLiteral("static"),
        QStringLiteral("static_assert"), QStringLiteral("static_cast"),
        QStringLiteral("struct"),       QStringLiteral("switch"),
        QStringLiteral("template"),     QStringLiteral("this"),
        QStringLiteral("thread_local"), QStringLiteral("throw"),
        QStringLiteral("true"),         QStringLiteral("try"),
        QStringLiteral("typedef"),      QStringLiteral("typeid"),
        QStringLiteral("typename"),     QStringLiteral("union"),
        QStringLiteral("unsigned"),     QStringLiteral("using"),
        QStringLiteral("virtual"),          QStringLiteral("void"),
        QStringLiteral("volatile"),     QStringLiteral("wchar_t"),
        QStringLiteral("while"),        QStringLiteral("xor"),
        QStringLiteral("xor_eq"),
    };
    if (cppKeywords.contains(result)) {
        result.prepend(QStringLiteral("Project_"));
    }
    return result;
}

bool nodeLess(const BlueprintNode *left, const BlueprintNode *right)
{
    const int idOrder = QString::compare(left->id, right->id, Qt::CaseSensitive);
    if (idOrder != 0) {
        return idOrder < 0;
    }
    const int typeOrder = static_cast<int>(left->type) - static_cast<int>(right->type);
    if (typeOrder != 0) {
        return typeOrder < 0;
    }
    return QString::compare(left->name, right->name, Qt::CaseSensitive) < 0;
}

bool neighborLess(const Neighbor &left, const Neighbor &right)
{
    const int nodeOrder = QString::compare(left.node->id, right.node->id, Qt::CaseSensitive);
    if (nodeOrder != 0) {
        return nodeOrder < 0;
    }
    const int edgeOrder = QString::compare(left.edge->id, right.edge->id, Qt::CaseSensitive);
    if (edgeOrder != 0) {
        return edgeOrder < 0;
    }
    return QString::compare(left.edge->label, right.edge->label, Qt::CaseSensitive) < 0;
}

QJsonObject upstreamInterface(const Neighbor &neighbor)
{
    return {
        {QStringLiteral("label"), neighbor.edge->label},
        {QStringLiteral("id"), neighbor.node->id},
        {QStringLiteral("type"), nodeTypeName(neighbor.node->type)},
        {QStringLiteral("name"), neighbor.node->name},
        {QStringLiteral("description"), neighbor.node->description},
        {QStringLiteral("outputs"), portsToJson(neighbor.node->outputs)},
    };
}

QJsonObject downstreamInterface(const Neighbor &neighbor)
{
    return {
        {QStringLiteral("label"), neighbor.edge->label},
        {QStringLiteral("id"), neighbor.node->id},
        {QStringLiteral("type"), nodeTypeName(neighbor.node->type)},
        {QStringLiteral("name"), neighbor.node->name},
        {QStringLiteral("description"), neighbor.node->description},
        {QStringLiteral("inputs"), portsToJson(neighbor.node->inputs)},
    };
}

QJsonObject moduleToJson(const BlueprintNode &module,
                         const BlueprintDocument &document,
                         const QHash<QString, const BlueprintNode *> &nodesById)
{
    QVector<Neighbor> upstream;
    QVector<Neighbor> downstream;
    for (const BlueprintEdge &edge : document.edges) {
        if (edge.target == module.id && nodesById.contains(edge.source)) {
            upstream.append({&edge, nodesById.value(edge.source)});
        }
        if (edge.source == module.id && nodesById.contains(edge.target)) {
            downstream.append({&edge, nodesById.value(edge.target)});
        }
    }
    std::sort(upstream.begin(), upstream.end(), neighborLess);
    std::sort(downstream.begin(), downstream.end(), neighborLess);

    QJsonArray upstreamJson;
    for (const Neighbor &neighbor : upstream) {
        upstreamJson.append(upstreamInterface(neighbor));
    }
    QJsonArray downstreamJson;
    for (const Neighbor &neighbor : downstream) {
        downstreamJson.append(downstreamInterface(neighbor));
    }

    return {
        {QStringLiteral("id"), module.id},
        {QStringLiteral("type"), nodeTypeName(module.type)},
        {QStringLiteral("name"), module.name},
        {QStringLiteral("description"), module.description},
        {QStringLiteral("inputs"), portsToJson(module.inputs)},
        {QStringLiteral("outputs"), portsToJson(module.outputs)},
        {QStringLiteral("constraints"), stringsToJson(module.constraints)},
        {QStringLiteral("acceptanceCriteria"), stringsToJson(module.acceptanceCriteria)},
        {QStringLiteral("upstream"), upstreamJson},
        {QStringLiteral("downstream"), downstreamJson},
    };
}

} // namespace

QJsonObject IrCompiler::compile(const BlueprintDocument &document)
{
    QHash<QString, const BlueprintNode *> nodesById;
    QVector<const BlueprintNode *> modules;
    for (const BlueprintNode &node : document.nodes) {
        nodesById.insert(node.id, &node);
        if (isGeneratable(node.type)) {
            modules.append(&node);
        }
    }
    std::sort(modules.begin(), modules.end(), nodeLess);

    QJsonArray moduleValues;
    for (const BlueprintNode *module : modules) {
        moduleValues.append(moduleToJson(*module, document, nodesById));
    }

    const QJsonObject project{
        {QStringLiteral("id"), document.projectId},
        {QStringLiteral("name"), document.projectName},
        {QStringLiteral("namespace"), projectNamespace(document)},
        {QStringLiteral("target"), document.target},
        {QStringLiteral("framework"), QStringLiteral("Qt 6 Widgets")},
        {QStringLiteral("languageStandard"), QStringLiteral("C++17")},
        {QStringLiteral("buildSystem"), QStringLiteral("CMake")},
        {QStringLiteral("codingConventions"),
         QJsonArray{QStringLiteral("Qt 6 idioms"), QStringLiteral("RAII")}},
        {QStringLiteral("goal"),
         QStringLiteral("Generate %1 from the blueprint module contracts.")
             .arg(document.projectName)},
    };

    return {
        {QStringLiteral("irVersion"), 1},
        {QStringLiteral("project"), project},
        {QStringLiteral("modules"), moduleValues},
    };
}

QByteArray IrCompiler::toCanonicalJson(const QJsonObject &ir)
{
    return QJsonDocument(ir).toJson(QJsonDocument::Compact);
}
