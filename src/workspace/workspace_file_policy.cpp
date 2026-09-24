#include "workspace/workspace_file_policy.h"
#include "generation/workspace_io.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace WorkspaceFilePolicy {
bool safeRelative(const QString &path)
{
    if (path.isEmpty() || QDir::isAbsolutePath(path) || path.contains('\\') || path.contains(':')) return false;
    static const QRegularExpression reserved("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$", QRegularExpression::CaseInsensitiveOption);
    for (const auto &part : path.split('/')) {
        if (part.isEmpty() || part == "." || part == ".." || part.endsWith('.') || part.endsWith(' ')
            || reserved.match(part.section('.', 0, 0)).hasMatch()) return false;
        for (auto c : part)
            if (c.unicode() < 32 || c.unicode() == 127 || QStringLiteral("<>\"|?*").contains(c)) return false;
    }
    return true;
}
bool rootPath(const QString &workspace, QString &canonicalRoot)
{
    canonicalRoot.clear();
    if (!WorkspaceIo::checkPath(workspace, "generated-project", nullptr)) return false;
    const QFileInfo info(QDir(workspace).filePath("generated-project"));
    if (!info.isDir()) return false;
    canonicalRoot = info.canonicalFilePath();
    return !canonicalRoot.isEmpty();
}
bool resolve(const QString &workspace, const QString &expectedRoot, const QString &relative, QString &absolute)
{
    absolute.clear();
    QString root;
    if (!safeRelative(relative) || !rootPath(workspace, root) || root != expectedRoot) return false;
    QString cursor = root;
    for (const auto &part : relative.split('/')) {
        cursor = QDir(cursor).filePath(part);
        const QFileInfo info(cursor);
        if (!info.exists() || info.isSymbolicLink()) return false;
#ifdef Q_OS_WIN
        const QString native = QDir::toNativeSeparators(cursor);
        const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(native.utf16()));
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
#endif
        const QString canonical = info.canonicalFilePath();
        if (canonical.isEmpty() || !safeRelative(QDir(root).relativeFilePath(canonical))) return false;
    }
    absolute = cursor;
    return true;
}
QString entryIdentity(const QString &absolute)
{
#ifdef Q_OS_WIN
    const QString native = QDir::toNativeSeparators(absolute);
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};
    BY_HANDLE_FILE_INFORMATION info{};
    const bool ok = GetFileInformationByHandle(handle, &info);
    CloseHandle(handle);
    if (!ok) return {};
    return QStringLiteral("%1:%2:%3").arg(info.dwVolumeSerialNumber)
        .arg(info.nFileIndexHigh).arg(info.nFileIndexLow);
#else
    struct stat info{};
    const QByteArray encoded = QFile::encodeName(absolute);
    if (::stat(encoded.constData(), &info) != 0) return {};
    return QStringLiteral("%1:%2").arg(qulonglong(info.st_dev)).arg(qulonglong(info.st_ino));
#endif
}
bool internal(const QString &relative)
{
    const auto parts = relative.toLower().split('/');
    for (const auto &part : parts)
        if (part == "candidates" || part == "generation-manifest.json" || part == "import-manifest.json"
            || part == ".git" || part == ".omp" || part == ".codex" || part == "cmakefiles"
            || part == "cmakecache.txt") return true;
    return false;
}
QJsonObject manifest(const QString &workspace)
{
    QJsonObject result;
    if (!WorkspaceIo::checkPath(workspace, "generation-manifest.json", nullptr)) return {};
    const QFileInfo info(QDir(workspace).filePath("generation-manifest.json"));
    if (!info.isFile() || info.size() > PreviewLimit || !WorkspaceIo::loadManifest(workspace, result, nullptr)) return {};
    return result;
}
Kind classify(const QString &relative, const QJsonObject &metadata)
{
    if (!safeRelative(relative) || internal(relative)) return Kind::Internal;
    const QString path = relative.toLower();
    if (path == "external" || path.startsWith("external/") || path == "src/external" || path.startsWith("src/external/"))
        return Kind::ExternalProtected;
    if (path == "src/contracts" || path.startsWith("src/contracts/")) return Kind::ProtectedScaffold;
    static const QRegularExpression scaffold("^(cmakelists\\.txt|readme\\.md|src/main\\.cpp|tests/scaffold_smoke\\.cpp|src/contracts/(blueprint\\.json|source-blueprint\\.json|types\\.h)|src/modules/[^/]+/(contract\\.h|implementation/placeholder\\.h)|tests/[^/]+/placeholder\\.h)$");
    if (scaffold.match(path).hasMatch()) return Kind::ProtectedScaffold;
    const auto protectedFiles = metadata.value("protectedFiles").toObject();
    for (auto it = protectedFiles.begin(); it != protectedFiles.end(); ++it)
        if (it.key().compare(relative, Qt::CaseInsensitive) == 0) return Kind::ProtectedScaffold;
    return metadata.isEmpty() ? Kind::UnknownProtected : Kind::OrdinaryFutureEditable;
}
}
