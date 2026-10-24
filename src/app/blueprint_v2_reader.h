#pragma once
#include <QObject>
#include <QJsonObject>
#include <QPointer>
class QProcess;
class QTimer;

// Validates the process envelope, never Blueprint schema or plugin semantics.
class BlueprintV2Reader final : public QObject {
    Q_OBJECT
public:
    explicit BlueprintV2Reader(QObject *parent = nullptr);
    ~BlueprintV2Reader() override;
    void setConfiguration(const QString &node, const QString &entry, int timeoutMs = 15000);
    void read(const QString &sourceText, const QJsonObject &registry);
    void cancel();
signals:
    void finished(const QJsonObject &reply, const QString &error);
private:
    void fail(const QString &error);
    QPointer<QProcess> m_process;
    QString m_node, m_entry, m_requestId;
    int m_timeout = 15000;
    QByteArray m_output;
    qsizetype m_stderrBytes = 0;
};
