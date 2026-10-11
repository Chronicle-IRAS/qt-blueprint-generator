#pragma once

#include "blueprint/blueprint_document.h"
#include <QMap>

// Pure, deterministic validation and source generation for the supported linear flow.
class RuntimeAssembler final
{
public:
    static bool assemble(const BlueprintDocument &document, const QString &scaffoldNamespace,
                         QMap<QString, QByteArray> &files, QString *errorMessage = nullptr);
};
