#pragma once

#include <QString>

class ProjectExporter final
{
public:
    static bool exportProject(const QString &workspacePath, const QString &targetPath,
                              QString *error = nullptr);

    ProjectExporter() = delete;
};
