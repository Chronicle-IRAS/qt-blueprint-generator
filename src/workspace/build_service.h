#pragma once

#include <QObject>
#include <QString>
#include <QStringConverter>
#include <QStringList>

class QProcess;

enum class BuildStage
{
    Configure,
    Build
};

struct BuildRequest
{
    QString sourceDirectory;
    QString buildDirectory;
    QString cmakeExecutable = QStringLiteral("cmake");
    QStringList configureArguments;
    QStringList buildArguments;
};

struct BuildResult
{
    bool success = false;
    bool configureStarted = false;
    int configureExitCode = -1;
    bool buildStarted = false;
    int buildExitCode = -1;
    QString standardOutput;
    QString standardError;
    QString error;
};

Q_DECLARE_METATYPE(BuildStage)
Q_DECLARE_METATYPE(BuildResult)

class BuildService final : public QObject
{
    Q_OBJECT

public:
    explicit BuildService(QObject *parent = nullptr);
    ~BuildService() override;

    bool start(const BuildRequest &request, QString *error = nullptr);
    bool isRunning() const;

signals:
    void standardOutput(BuildStage stage, const QString &text);
    void standardError(BuildStage stage, const QString &text);
    void stageFinished(BuildStage stage, int exitCode);
    void finished(const BuildResult &result);

private:
    void startStage(BuildStage stage);
    bool collectOutput();
    void processFinished(int exitCode, int exitStatus);
    void processError(int processError);
    void finish(const QString &error = {});

    QProcess *m_process = nullptr;
    BuildRequest m_request;
    BuildResult m_result;
    BuildStage m_stage = BuildStage::Configure;
    bool m_running = false;
    QStringDecoder m_stdoutDecoder{QStringDecoder::Utf8};
    QStringDecoder m_stderrDecoder{QStringDecoder::Utf8};
};
