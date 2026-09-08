#include "workspace/build_service.h"

#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

bool safeAbsolutePath(const QString &path, bool mustExist, QString *error)
{
    if (!QDir::isAbsolutePath(path))
        return fail(error, QStringLiteral("Source and build paths must be absolute"));
    const QString requested = QDir::cleanPath(path);
    QString cursor = requested;
    while (true) {
        const QFileInfo info(cursor);
        bool link = info.isSymbolicLink();
#ifdef Q_OS_WIN
        const QString native = QDir::toNativeSeparators(cursor);
        const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(native.utf16()));
        link = link || (attributes != INVALID_FILE_ATTRIBUTES
                        && (attributes & FILE_ATTRIBUTE_REPARSE_POINT));
#endif
        if (link)
            return fail(error, QStringLiteral("Build paths must not contain links or reparse points: ") + cursor);
        if (info.exists()) {
            if (!info.isDir())
                return fail(error, cursor == requested
                    ? QStringLiteral("Build paths must name directories")
                    : QStringLiteral("A build path ancestor is not a directory: ") + cursor);
            const QDir parent(info.absolutePath());
            for (const QString &entry : parent.entryList(QDir::AllEntries | QDir::Hidden
                                                          | QDir::System | QDir::NoDotAndDotDot)) {
                if (entry.compare(info.fileName(), Qt::CaseInsensitive) == 0
                    && entry != info.fileName())
                    return fail(error, QStringLiteral("Build path case alias is forbidden: ") + cursor);
            }
        }
        const QString parent = info.absolutePath();
        if (parent == cursor)
            break;
        cursor = parent;
    }
    if (mustExist && !QFileInfo(requested).isDir())
        return fail(error, QStringLiteral("Source directory does not exist"));
    return true;
}

bool pathsOverlap(const QString &left, const QString &right)
{
    QString a = QDir::cleanPath(left);
    QString b = QDir::cleanPath(right);
#ifdef Q_OS_WIN
    a = a.toCaseFolded();
    b = b.toCaseFolded();
#endif
    return a == b || a.startsWith(b + QLatin1Char('/')) || b.startsWith(a + QLatin1Char('/'));
}

bool reservedLongOption(const QString &argument, const QString &option)
{
    return argument == option || argument.startsWith(option + QLatin1Char('='));
}

bool validConfigureArguments(const QStringList &arguments, QString *error)
{
    for (const QString &argument : arguments) {
        const bool ownsShortPath = argument == QStringLiteral("-S")
            || argument == QStringLiteral("-B") || argument == QStringLiteral("-P")
            || (argument.size() > 2 && (argument.startsWith(QStringLiteral("-S"))
                                        || argument.startsWith(QStringLiteral("-B"))
                                        || argument.startsWith(QStringLiteral("-P"))));
        const bool ownsLongPath = reservedLongOption(argument, QStringLiteral("--source"))
            || reservedLongOption(argument, QStringLiteral("--build"));
        const bool alternateMode = argument == QStringLiteral("-E")
            || reservedLongOption(argument, QStringLiteral("--install"))
            || reservedLongOption(argument, QStringLiteral("--open"))
            || reservedLongOption(argument, QStringLiteral("--workflow"))
            || reservedLongOption(argument, QStringLiteral("--find-package"));
        if (ownsShortPath || ownsLongPath || alternateMode)
            return fail(error, QStringLiteral("Reserved CMake configure argument is not allowed: ")
                                   + argument);
    }
    return true;
}
}

BuildService::BuildService(QObject *parent)
    : QObject(parent), m_process(new QProcess(this))
{
    qRegisterMetaType<BuildStage>();
    qRegisterMetaType<BuildResult>();
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &BuildService::collectOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &BuildService::collectOutput);
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) { processFinished(code, int(status)); });
    connect(m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) { processError(int(error)); });
}

BuildService::~BuildService()
{
    m_process->disconnect(this);
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(3000);
    }
}

bool BuildService::start(const BuildRequest &request, QString *error)
{
    if (error)
        error->clear();
    if (m_running)
        return fail(error, QStringLiteral("A build is already running"));
    if (request.cmakeExecutable.trimmed().isEmpty())
        return fail(error, QStringLiteral("CMake executable must not be empty"));
    if (!validConfigureArguments(request.configureArguments, error))
        return false;
    if (!safeAbsolutePath(request.sourceDirectory, true, error)
        || !safeAbsolutePath(request.buildDirectory, false, error))
        return false;
    if (pathsOverlap(request.sourceDirectory, request.buildDirectory))
        return fail(error, QStringLiteral("Source and build directories must not overlap"));

    m_request = request;
    m_result = {};
    m_stdoutDecoder = QStringDecoder(QStringDecoder::Utf8);
    m_stderrDecoder = QStringDecoder(QStringDecoder::Utf8);
    m_running = true;
    startStage(BuildStage::Configure);
    return true;
}

bool BuildService::isRunning() const
{
    return m_running;
}

void BuildService::startStage(BuildStage stage)
{
    m_stage = stage;
    QStringList arguments;
    if (stage == BuildStage::Configure) {
        m_result.configureStarted = true;
        arguments = {QStringLiteral("-S"), m_request.sourceDirectory,
                     QStringLiteral("-B"), m_request.buildDirectory};
        arguments.append(m_request.configureArguments);
    } else {
        m_result.buildStarted = true;
        arguments = {QStringLiteral("--build"), m_request.buildDirectory};
        arguments.append(m_request.buildArguments);
    }
    m_process->start(m_request.cmakeExecutable, arguments);
}

bool BuildService::collectOutput()
{
    const QString output = m_stdoutDecoder(m_process->readAllStandardOutput());
    const QString error = m_stderrDecoder(m_process->readAllStandardError());
    QPointer<BuildService> self(this);
    if (!output.isEmpty()) {
        m_result.standardOutput += output;
        emit standardOutput(m_stage, output);
        if (!self)
            return false;
    }
    if (!error.isEmpty()) {
        m_result.standardError += error;
        emit standardError(m_stage, error);
        if (!self)
            return false;
    }
    return true;
}

void BuildService::processFinished(int exitCode, int exitStatus)
{
    if (!m_running)
        return;
    QPointer<BuildService> self(this);
    if (!collectOutput() || !self)
        return;
    const bool normal = exitStatus == int(QProcess::NormalExit);
    if (m_stage == BuildStage::Configure)
        m_result.configureExitCode = exitCode;
    else
        m_result.buildExitCode = exitCode;
    emit stageFinished(m_stage, exitCode);
    if (!self)
        return;
    if (!normal || exitCode != 0) {
        const QString stage = m_stage == BuildStage::Configure
            ? QStringLiteral("Configure") : QStringLiteral("Build");
        finish(QStringLiteral("%1 failed with exit code %2").arg(stage).arg(exitCode));
        return;
    }
    if (m_stage == BuildStage::Configure) {
        startStage(BuildStage::Build);
        return;
    }
    m_result.success = true;
    finish();
}

void BuildService::processError(int processError)
{
    if (!m_running || processError != int(QProcess::FailedToStart))
        return;
    QPointer<BuildService> self(this);
    if (!collectOutput() || !self)
        return;
    finish(QStringLiteral("Could not start CMake: ") + m_process->errorString());
}

void BuildService::finish(const QString &error)
{
    if (!m_running)
        return;
    m_result.error = error;
    m_running = false;
    const BuildResult result = m_result;
    emit finished(result);
}
