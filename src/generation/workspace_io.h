#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <optional>

// Trusted local workspace, single writer. Path-based Qt operations cannot defend
// against a hostile process replacing directories concurrently with these checks.
namespace WorkspaceIo {
bool fail(QString *error, const QString &message);
QString sha256(const QByteArray &bytes);
bool safeId(const QString &id);
bool safeRelative(const QString &path);
bool checkPath(const QString &workspace, const QString &relative, QString *error);
bool read(const QString &workspace, const QString &relative, std::optional<QByteArray> &bytes, QString *error);
bool transaction(const QString &workspace, const QMap<QString, QByteArray> &writes, QString *error);
bool loadManifest(const QString &workspace, QJsonObject &manifest, QString *error);
bool ownedPath(const QString &nodeId, const QString &path);
QByteArray json(const QJsonObject &object);
}
