#pragma once
#include "blueprint/blueprint_document.h"

class ProjectScaffolder final
{
public:
    static bool create(const BlueprintDocument &document, const QString &absoluteWorkspace,
                       QString *errorMessage = nullptr);
};
