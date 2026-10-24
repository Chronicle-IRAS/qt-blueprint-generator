#include "app/blueprint_v2_reader.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QTimer>
#include <QUuid>

namespace {
constexpr qsizetype MessageLimit = 32 * 1024 * 1024;
bool stringFields(const QJsonObject &object, std::initializer_list<const char *> fields) {
    for (auto field : fields) if (!object[field].isString()) return false;
    return true;
}
bool endpointShape(const QJsonValue &value, bool channel) {
    if (!value.isObject()) return false;
    const auto endpoint = value.toObject();
    return stringFields(endpoint, {"nodeId", "portId"}) && (!channel || endpoint["unitId"].isString());
}
bool validReply(const QJsonObject &reply, const QString &id) {
    if (reply["protocolVersion"] != 1 || reply["requestId"] != id || reply["canGenerate"] != false ||
        !reply["diagnostics"].isArray()) return false;
    for (const auto &value : reply["diagnostics"].toArray()) {
        if (!value.isObject()) return false;
        auto diagnostic = value.toObject();
        if (!stringFields(diagnostic, {"code", "severity", "message", "path"}) || diagnostic["severity"] != "error") return false;
    }
    const auto status = reply["status"].toString();
    if (status == "error" || status == "rejected") return true;
    if (status != "read" || !reply["semanticValid"].isBool() || !reply["document"].isObject() || !reply["nodes"].isArray()) return false;
    const auto document = reply["document"].toObject();
    for (auto key : {"nodes", "edges", "channels"}) if (!document[key].isArray()) return false;
    for (auto key : {"edges", "channels"}) for (const auto value : document[key].toArray()) {
        if (!value.isObject()) return false;
        const auto relation = value.toObject();
        const bool channel = QString::fromLatin1(key) == "channels";
        if (!relation["id"].isString() || !endpointShape(relation["source"], channel) || !endpointShape(relation["target"], channel)) return false;
        if (channel ? (!relation["wireSchema"].isObject() || !relation["bridge"].isObject()) : !relation["kind"].isString()) return false;
    }
    const auto nodes = reply["nodes"].toArray();
    if (nodes.size() != document["nodes"].toArray().size()) return false;
    for (qsizetype i = 0; i < nodes.size(); ++i) {
        if (!nodes[i].isObject()) return false;
        const auto node = nodes[i].toObject();
        if (node["sourceIndex"] != i || !node["data"].isObject()) return false;
        if (!stringFields(node, {"nodeId", "unitId", "typeId", "nodeDefinitionVersion"})) return false;
        const auto data = node["data"].toObject();
        if (data != document["nodes"].toArray()[i].toObject() || !data["ports"].isArray() || !data.contains("properties")) return false;
        for (const auto value : data["ports"].toArray())
            if (!value.isObject() || !stringFields(value.toObject(), {"id", "name", "direction", "kind", "typeId"})) return false;
        if (node["kind"] == "known") { if (!node["title"].isString()) return false; }
        else if (node["kind"] == "unknown") {
            if (!node["reasonCodes"].isArray()) return false;
            for (auto reason : node["reasonCodes"].toArray()) if (!reason.isString()) return false;
        } else return false;
    }
    return true;
}
}
BlueprintV2Reader::BlueprintV2Reader(QObject *parent) : QObject(parent) {
    m_node = QStandardPaths::findExecutable(QStringLiteral("node"));
    m_entry = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("v2-reader/tools/schema-contracts/blueprint-reader-cli.js"));
}
BlueprintV2Reader::~BlueprintV2Reader() { cancel(); }
void BlueprintV2Reader::setConfiguration(const QString &node, const QString &entry, int timeoutMs) {
    cancel(); m_node = node; m_entry = entry; m_timeout = qBound(1, timeoutMs, 15000);
}
void BlueprintV2Reader::cancel() {
    m_requestId.clear();
    if (!m_process) return;
    auto *process = m_process.data(); m_process = nullptr;
    disconnect(process, nullptr, this, nullptr);
    process->setParent(nullptr);
    process->kill();
    // QProcess deletes the native process resources after termination; no GUI wait.
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process, &QObject::deleteLater);
    if (process->state() == QProcess::NotRunning) process->deleteLater();
}
void BlueprintV2Reader::fail(const QString &error) { cancel(); emit finished({}, error); }
void BlueprintV2Reader::read(const QString &sourceText, const QJsonObject &registry) {
    cancel(); m_output.clear(); m_stderrBytes = 0;
    m_requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (m_node.isEmpty() || !QFileInfo::exists(m_entry)) {
        const auto id = m_requestId;
        QTimer::singleShot(0, this, [this, id] { if (id == m_requestId) fail(tr("Reader runtime unavailable. Install Node and run npm ci, then build the blueprint_v2_reader target.")); });
        return;
    }
    const auto input = QJsonDocument(QJsonObject{{"protocolVersion", 1}, {"requestId", m_requestId}, {"sourceText", sourceText}, {"registry", registry}}).toJson(QJsonDocument::Compact);
    if (sourceText.toUtf8().size() > 8 * 1024 * 1024 || input.size() > MessageLimit) {
        const auto id = m_requestId;
        QTimer::singleShot(0, this, [this, id] { if (id == m_requestId) fail(tr("Reader request exceeds the size limit.")); }); return;
    }
    auto *process = new QProcess(this); m_process = process;
    QProcessEnvironment environment;
    const auto system = QProcessEnvironment::systemEnvironment();
    for (auto key : {"PATH", "SystemRoot", "WINDIR", "TEMP", "TMP", "LANG", "LC_ALL"})
        if (system.contains(key)) environment.insert(key, system.value(key));
    process->setProcessEnvironment(environment);
    process->setProgram(m_node); process->setArguments({m_entry});
    connect(process, &QProcess::started, this, [process, input] { process->write(input); process->closeWriteChannel(); });
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        m_output += process->readAllStandardOutput();
        if (m_output.size() > MessageLimit) fail(tr("Reader reply exceeds the size limit."));
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, process] {
        m_stderrBytes += process->readAllStandardError().size();
        if (m_stderrBytes > 64 * 1024) fail(tr("Reader exceeded its diagnostic output limit."));
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError) {
        if (m_process == process) fail(tr("Reader process could not run. Check the configured Node runtime and entry point."));
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, process](int code, QProcess::ExitStatus status) {
        if (m_process != process) return;
        m_output += process->readAllStandardOutput();
        m_stderrBytes += process->readAllStandardError().size();
        if (m_stderrBytes > 64 * 1024) { fail(tr("Reader exceeded its diagnostic output limit.")); return; }
        if (code || status != QProcess::NormalExit) { fail(tr("Reader process failed.")); return; }
        QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
        const QString decoded = decoder.decode(m_output);
        QJsonParseError parse;
        const auto json = QJsonDocument::fromJson(decoded.toUtf8(), &parse);
        if (m_output.size() > MessageLimit || decoder.hasError() || parse.error != QJsonParseError::NoError || !json.isObject() || !validReply(json.object(), m_requestId)) {
            fail(tr("Reader returned an invalid or uncorrelated reply.")); return;
        }
        m_process = nullptr; process->deleteLater(); emit finished(json.object(), {});
    });
    auto *timer = new QTimer(process); timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, process] { if (m_process == process) fail(tr("Reader timed out.")); });
    timer->start(m_timeout); process->start();
}
