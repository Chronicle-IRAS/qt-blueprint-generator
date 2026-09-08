#pragma once

#include <QString>

namespace ProjectExporterRecovery {
struct Decision
{
    bool removeRollback = false;
    QString error;
};

Decision decideBackupRemovalFailure(bool moved, bool restored,
                                    const QString &targetPath,
                                    const QString &backupPath,
                                    const QString &rollbackPath);
}
