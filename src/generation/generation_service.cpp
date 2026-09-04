#include "generation/generation_service.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>
#include <QStringList>
#include <QTimer>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <utility>

namespace {

#ifdef Q_OS_WIN
constexpr Qt::CaseSensitivity pathCaseSensitivity = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity pathCaseSensitivity = Qt::CaseSensitive;
#endif

bool fail(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
    return false;
}

bool validLimits(const GenerationLimits &limits, QString *errorMessage)
{
    if (limits.maxResponseBytes < 0 || limits.maxFileBytes < 0
        || limits.maxTotalContentBytes < 0 || limits.maxFiles < 0
        || limits.maxWireResponseBytes < 0) {
        return fail(errorMessage, QStringLiteral("Generation limits must not be negative"));
    }
    return true;
}

bool readRequiredString(const QJsonObject &object,
                        const QString &key,
                        const QString &objectPath,
                        QString &result,
                        QString *errorMessage)
{
    const QString path = objectPath + QLatin1Char('.') + key;
    if (!object.contains(key)) {
        return fail(errorMessage, QStringLiteral("%1 is required").arg(path));
    }
    const QJsonValue value = object.value(key);
    if (!value.isString()) {
        return fail(errorMessage, QStringLiteral("%1 must be a string").arg(path));
    }
    result = value.toString();
    return true;
}

bool hasOnlyRequiredFields(const QJsonObject &object,
                           const QStringList &requiredFields,
                           const QString &objectPath,
                           QString *errorMessage)
{
    for (const QString &field : requiredFields) {
        if (!object.contains(field)) {
            return fail(errorMessage,
                        QStringLiteral("%1.%2 is required").arg(objectPath, field));
        }
    }
    if (object.size() != requiredFields.size()) {
        return fail(errorMessage,
                    QStringLiteral("%1 contains unsupported fields").arg(objectPath));
    }
    return true;
}

bool isReservedWindowsName(const QString &segment)
{
    const QString stem = segment.section(QLatin1Char('.'), 0, 0).toUpper();
    if (stem == QStringLiteral("CON") || stem == QStringLiteral("PRN")
        || stem == QStringLiteral("AUX") || stem == QStringLiteral("NUL")) {
        return true;
    }
    if (stem.size() == 4
        && (stem.startsWith(QStringLiteral("COM"))
            || stem.startsWith(QStringLiteral("LPT")))) {
        const QChar number = stem.back();
        return number >= QLatin1Char('1') && number <= QLatin1Char('9');
    }
    return false;
}

bool containsInvalidPathCharacter(const QString &segment)
{
    static const QString invalidCharacters = QStringLiteral(":*?\"<>|");
    for (const QChar character : segment) {
        if (character.unicode() < 0x20 || character.unicode() == 0x7f
            || invalidCharacters.contains(character)) {
            return true;
        }
    }
    return false;
}

QString canonicalExistingPath(const QString &path)
{
#ifdef Q_OS_WIN
    const QString nativePath = QDir::toNativeSeparators(path);
    const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(nativePath.utf16()),
                                      0,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr,
                                      OPEN_EXISTING,
                                      FILE_FLAG_BACKUP_SEMANTICS,
                                      nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return {};
    }

    const DWORD requiredLength =
        GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (requiredLength == 0) {
        CloseHandle(handle);
        return {};
    }
    QString resolvedPath(static_cast<qsizetype>(requiredLength), Qt::Uninitialized);
    const DWORD writtenLength = GetFinalPathNameByHandleW(
        handle,
        reinterpret_cast<LPWSTR>(resolvedPath.data()),
        requiredLength,
        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    CloseHandle(handle);
    if (writtenLength == 0 || writtenLength >= requiredLength) {
        return {};
    }
    resolvedPath.truncate(static_cast<qsizetype>(writtenLength));
    resolvedPath = QDir::fromNativeSeparators(resolvedPath);
    if (resolvedPath.startsWith(QStringLiteral("//?/UNC/"), Qt::CaseInsensitive)) {
        resolvedPath = QStringLiteral("//") + resolvedPath.mid(8);
    } else if (resolvedPath.startsWith(QStringLiteral("//?/"), Qt::CaseInsensitive)) {
        resolvedPath.remove(0, 4);
    }
    return QDir::cleanPath(resolvedPath);
#else
    return QFileInfo(path).canonicalFilePath();
#endif
}

bool isSameOrChildPath(const QString &path, const QString &parentPath)
{
    const QString normalizedPath = QDir::fromNativeSeparators(QDir::cleanPath(path));
    const QString normalizedParent = QDir::fromNativeSeparators(QDir::cleanPath(parentPath));
    if (normalizedPath.compare(normalizedParent, pathCaseSensitivity) == 0) {
        return true;
    }
    const QString parentPrefix = normalizedParent.endsWith(QLatin1Char('/'))
                                     ? normalizedParent
                                     : normalizedParent + QLatin1Char('/');
    return normalizedPath.startsWith(parentPrefix, pathCaseSensitivity);
}

bool existingAncestorsStayWithinCandidateRoot(const QString &absolutePath,
                                              const QString &candidateRoot,
                                              QString *errorMessage)
{
    const QFileInfo rootInfo(candidateRoot);
    if (!rootInfo.exists()) {
        return true;
    }
    if (!rootInfo.isDir()) {
        return fail(errorMessage,
                    QStringLiteral("Candidate directory must refer to a directory"));
    }

    const QString canonicalRoot = canonicalExistingPath(candidateRoot);
    if (canonicalRoot.isEmpty()) {
        return fail(errorMessage,
                    QStringLiteral("Candidate directory cannot be resolved safely"));
    }

    QString existingPath = absolutePath;
    while (true) {
        const QFileInfo info(existingPath);
        if (info.exists() || info.isSymbolicLink()) {
            break;
        }
        const QString parentPath = info.absolutePath();
        if (parentPath == existingPath) {
            return fail(errorMessage,
                        QStringLiteral("Candidate target has no resolvable ancestor"));
        }
        existingPath = parentPath;
    }

    const QString canonicalAncestor = canonicalExistingPath(existingPath);
    if (canonicalAncestor.isEmpty()
        || !isSameOrChildPath(canonicalAncestor, canonicalRoot)) {
        return fail(errorMessage,
                    QStringLiteral("Candidate target resolves outside the candidate directory"));
    }
    return true;
}

std::optional<GeneratedFile> validatedFile(const QString &rawPath,
                                           const QString &content,
                                           const QString &candidateRoot,
                                           QString *errorMessage)
{
    if (rawPath.isEmpty()) {
        fail(errorMessage, QStringLiteral("Generated file path must not be empty"));
        return std::nullopt;
    }

    QString portablePath = rawPath;
    portablePath.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const bool hasDrivePrefix = portablePath.size() >= 2
                                && portablePath.at(0).isLetter()
                                && portablePath.at(1) == QLatin1Char(':');
    if (portablePath.startsWith(QLatin1Char('/')) || hasDrivePrefix
        || QDir::isAbsolutePath(rawPath) || QDir::isAbsolutePath(portablePath)) {
        fail(errorMessage,
             QStringLiteral("Generated file path '%1' must be relative").arg(rawPath));
        return std::nullopt;
    }

    const QStringList segments = portablePath.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    for (const QString &segment : segments) {
        if (segment.isEmpty()) {
            fail(errorMessage,
                 QStringLiteral("Generated file path '%1' contains an empty segment")
                     .arg(rawPath));
            return std::nullopt;
        }
        if (segment == QStringLiteral(".") || segment == QStringLiteral("..")) {
            fail(errorMessage,
                 QStringLiteral("Generated file path '%1' contains a forbidden segment")
                     .arg(rawPath));
            return std::nullopt;
        }
        if (segment.endsWith(QLatin1Char('.')) || segment.endsWith(QLatin1Char(' '))
            || containsInvalidPathCharacter(segment) || isReservedWindowsName(segment)) {
            fail(errorMessage,
                 QStringLiteral("Generated file path '%1' is not portable or safe").arg(rawPath));
            return std::nullopt;
        }
    }

    const QString normalizedPath = QDir::cleanPath(portablePath);
    const QString extension = QFileInfo(normalizedPath).suffix().toLower();
    static const QSet<QString> allowedExtensions{
        QStringLiteral("h"),
        QStringLiteral("hpp"),
        QStringLiteral("cpp"),
        QStringLiteral("cc"),
    };
    if (!allowedExtensions.contains(extension)) {
        fail(errorMessage,
             QStringLiteral("Generated file path '%1' has an unsupported extension")
                 .arg(rawPath));
        return std::nullopt;
    }

    const QString absolutePath =
        QDir::cleanPath(QDir(candidateRoot).absoluteFilePath(normalizedPath));
    const QString rootPrefix = candidateRoot.endsWith(QLatin1Char('/'))
                                   ? candidateRoot
                                   : candidateRoot + QLatin1Char('/');
    if (absolutePath.compare(candidateRoot, Qt::CaseInsensitive) == 0
        || !absolutePath.startsWith(rootPrefix, Qt::CaseInsensitive)) {
        fail(errorMessage,
             QStringLiteral("Generated file path '%1' escapes the candidate directory")
                 .arg(rawPath));
        return std::nullopt;
    }
    if (!existingAncestorsStayWithinCandidateRoot(absolutePath,
                                                  candidateRoot,
                                                  errorMessage)) {
        return std::nullopt;
    }

    return GeneratedFile{normalizedPath, absolutePath, content};
}

} // namespace

GenerationService::GenerationService(IAiClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
{
    if (client == nullptr) {
        return;
    }
    connect(client,
            &IAiClient::responseReady,
            this,
            &GenerationService::handleResponse);
    connect(client,
            &IAiClient::requestFailed,
            this,
            &GenerationService::handleFailure);
    connect(client, &QObject::destroyed, this, [this]() {
        const QList<QUuid> requestIds = m_pending.keys();
        m_pending.clear();
        for (const QUuid &requestId : requestIds) {
            emit generationFailed(requestId, QStringLiteral("AI client became unavailable"));
        }
    });
}

QUuid GenerationService::generate(const QString &prompt,
                                  const QString &expectedNodeId,
                                  const QString &absoluteCandidateDirectory,
                                  const GenerationLimits &limits)
{
    const QUuid requestId = QUuid::createUuid();
    QString validationError;
    const QString normalizedRoot =
        QDir::cleanPath(QDir::fromNativeSeparators(absoluteCandidateDirectory));
    if (!QDir::isAbsolutePath(absoluteCandidateDirectory)
        || !validLimits(limits, &validationError) || m_client.isNull()) {
        if (validationError.isEmpty()) {
            validationError = m_client.isNull()
                                  ? QStringLiteral("AI client is unavailable")
                                  : QStringLiteral("Candidate directory must be absolute");
        }
        QTimer::singleShot(0, this, [this, requestId, validationError]() {
            emit generationFailed(requestId, validationError);
        });
        return requestId;
    }

    m_pending.insert(requestId,
                     RequestContext{expectedNodeId, normalizedRoot, limits});
    m_client->generate(AiRequest{requestId, prompt, limits.maxWireResponseBytes});
    return requestId;
}

std::optional<GenerationResult> GenerationService::parseAndValidate(
    const QByteArray &modelResponse,
    const QString &expectedNodeId,
    const QString &absoluteCandidateDirectory,
    const GenerationLimits &limits,
    QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!validLimits(limits, errorMessage)) {
        return std::nullopt;
    }
    if (modelResponse.size() > limits.maxResponseBytes) {
        fail(errorMessage,
             QStringLiteral("AI response exceeds the configured maximum response size"));
        return std::nullopt;
    }
    if (!QDir::isAbsolutePath(absoluteCandidateDirectory)) {
        fail(errorMessage, QStringLiteral("Candidate directory must be absolute"));
        return std::nullopt;
    }
    const QString candidateRoot =
        QDir::cleanPath(QDir::fromNativeSeparators(absoluteCandidateDirectory));

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(modelResponse, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        fail(errorMessage,
             QStringLiteral("Invalid model response JSON at offset %1: %2")
                 .arg(parseError.offset)
                 .arg(parseError.errorString()));
        return std::nullopt;
    }
    if (!document.isObject()) {
        fail(errorMessage, QStringLiteral("Model response root must be an object"));
        return std::nullopt;
    }

    const QJsonObject root = document.object();
    const QStringList rootFields{
        QStringLiteral("nodeId"),
        QStringLiteral("summary"),
        QStringLiteral("files"),
    };
    if (!hasOnlyRequiredFields(root, rootFields, QStringLiteral("root"), errorMessage)) {
        return std::nullopt;
    }

    GenerationResult result;
    if (!readRequiredString(root,
                            QStringLiteral("nodeId"),
                            QStringLiteral("root"),
                            result.nodeId,
                            errorMessage)
        || !readRequiredString(root,
                               QStringLiteral("summary"),
                               QStringLiteral("root"),
                               result.summary,
                               errorMessage)) {
        return std::nullopt;
    }
    if (result.nodeId != expectedNodeId) {
        fail(errorMessage,
             QStringLiteral("Model response nodeId '%1' does not match requested nodeId '%2'")
                 .arg(result.nodeId, expectedNodeId));
        return std::nullopt;
    }

    const QJsonValue filesValue = root.value(QStringLiteral("files"));
    if (!filesValue.isArray()) {
        fail(errorMessage, QStringLiteral("root.files must be an array"));
        return std::nullopt;
    }
    const QJsonArray files = filesValue.toArray();
    if (files.isEmpty()) {
        fail(errorMessage, QStringLiteral("root.files must not be empty"));
        return std::nullopt;
    }
    if (files.size() > limits.maxFiles) {
        fail(errorMessage,
             QStringLiteral("root.files exceeds the configured maximum files count"));
        return std::nullopt;
    }

    result.files.reserve(files.size());
    QSet<QString> normalizedPaths;
    qint64 totalContentBytes = 0;
    const QStringList fileFields{QStringLiteral("path"), QStringLiteral("content")};
    for (qsizetype index = 0; index < files.size(); ++index) {
        const QString filePath = QStringLiteral("root.files[%1]").arg(index);
        const QJsonValue fileValue = files.at(index);
        if (!fileValue.isObject()) {
            fail(errorMessage, QStringLiteral("%1 must be an object").arg(filePath));
            return std::nullopt;
        }

        const QJsonObject fileObject = fileValue.toObject();
        if (!hasOnlyRequiredFields(fileObject, fileFields, filePath, errorMessage)) {
            return std::nullopt;
        }
        QString path;
        QString content;
        if (!readRequiredString(fileObject,
                                QStringLiteral("path"),
                                filePath,
                                path,
                                errorMessage)
            || !readRequiredString(fileObject,
                                   QStringLiteral("content"),
                                   filePath,
                                   content,
                                   errorMessage)) {
            return std::nullopt;
        }

        const qint64 contentBytes = content.toUtf8().size();
        if (contentBytes > limits.maxFileBytes) {
            fail(errorMessage,
                 QStringLiteral("%1.content exceeds the configured maximum file size")
                     .arg(filePath));
            return std::nullopt;
        }
        if (contentBytes > limits.maxTotalContentBytes
            || totalContentBytes > limits.maxTotalContentBytes - contentBytes) {
            fail(errorMessage,
                 QStringLiteral("Total generated file content exceeds the configured limit"));
            return std::nullopt;
        }
        totalContentBytes += contentBytes;

        const std::optional<GeneratedFile> generatedFile =
            validatedFile(path, content, candidateRoot, errorMessage);
        if (!generatedFile.has_value()) {
            return std::nullopt;
        }
        const QString duplicateKey = generatedFile->relativePath.toCaseFolded();
        if (normalizedPaths.contains(duplicateKey)) {
            fail(errorMessage,
                 QStringLiteral("Generated files contain duplicate normalized path '%1'")
                     .arg(generatedFile->relativePath));
            return std::nullopt;
        }
        normalizedPaths.insert(duplicateKey);
        result.files.append(*generatedFile);
    }

    return result;
}

void GenerationService::handleResponse(const QUuid &requestId,
                                       const QByteArray &modelResponse)
{
    auto pendingIt = m_pending.find(requestId);
    if (pendingIt == m_pending.end()) {
        return;
    }
    const RequestContext context = pendingIt.value();
    m_pending.erase(pendingIt);

    QString errorMessage;
    const std::optional<GenerationResult> result =
        parseAndValidate(modelResponse,
                         context.expectedNodeId,
                         context.absoluteCandidateDirectory,
                         context.limits,
                         &errorMessage);
    if (!result.has_value()) {
        emit generationFailed(requestId, errorMessage);
        return;
    }
    emit generationSucceeded(requestId, *result);
}

void GenerationService::handleFailure(const QUuid &requestId, const QString &errorMessage)
{
    auto pendingIt = m_pending.find(requestId);
    if (pendingIt == m_pending.end()) {
        return;
    }
    m_pending.erase(pendingIt);
    emit generationFailed(requestId,
                          errorMessage.isEmpty() ? QStringLiteral("AI request failed")
                                                 : errorMessage);
}
