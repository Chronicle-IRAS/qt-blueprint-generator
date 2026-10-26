#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QPointer>
class QProcess;
class QTimer;
enum class BlueprintV2TransportOutcome { Reply, NotDispatched, Unknown };
Q_DECLARE_METATYPE(BlueprintV2TransportOutcome)

// Validates the process envelope, never Blueprint schema or plugin semantics.
class BlueprintV2Reader final : public QObject {
    Q_OBJECT
public:
    explicit BlueprintV2Reader(QObject *parent = nullptr);
    ~BlueprintV2Reader() override;
    void setConfiguration(const QString &node, const QString &entry, int timeoutMs = 15000);
    void read(const QString &sourceText, const QJsonObject &registry);
    void load(const QString &directory, const QJsonObject &registry);
    void preview(const QString &sourceText, const QJsonArray &edits, const QJsonObject &registry);
    void save(const QString &directory, const QString &sourceText, const QString &sha256, const QJsonArray &edits, const QJsonObject &registry);
    bool isSaving() const { return m_operation == "save" && !m_requestId.isEmpty(); }
    void cancel();
signals:
    void finished(const QJsonObject &reply, const QString &error, BlueprintV2TransportOutcome outcome);
private:
    void fail(const QString &error);
    void dispatch(QJsonObject request);
    void stopProcess();
    QPointer<QProcess> m_process;
    QString m_node, m_entry, m_requestId;
    QString m_operation;
    bool m_dispatched = false;
    bool m_failing = false;
    int m_timeout = 15000;
    QByteArray m_output;
    qsizetype m_stderrBytes = 0;
};
