#pragma once
#include "blueprint/blueprint_document.h"

class ProjectScaffolder final
{
public:
    enum class Mode { ModulesOnly, LinearRuntime };
    static bool create(const BlueprintDocument &document, const QString &absoluteWorkspace,
                       QString *errorMessage = nullptr);
    static bool createRuntime(const BlueprintDocument &document, const QString &absoluteWorkspace,
                              QString *errorMessage = nullptr);
private:
    static bool createInMode(const BlueprintDocument &document, const QString &absoluteWorkspace,
                             Mode mode, QString *errorMessage);
};
