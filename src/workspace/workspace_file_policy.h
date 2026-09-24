#pragma once
#include <QJsonObject>
#include <QString>

// Workspace file boundary and protection policy; generation rules are unchanged.
namespace WorkspaceFilePolicy {
enum class Kind { OrdinaryFutureEditable, ProtectedScaffold, ExternalProtected, Internal, UnknownProtected };
inline constexpr qint64 PreviewLimit = 1024 * 1024;
bool safeRelative(const QString &path);
bool rootPath(const QString &workspace, QString &canonicalRoot);
bool resolve(const QString &workspace, const QString &expectedRoot, const QString &relative, QString &absolute);
QString entryIdentity(const QString &absolute);
bool internal(const QString &relative);
QJsonObject manifest(const QString &workspace);
Kind classify(const QString &relative, const QJsonObject &manifest);
}
