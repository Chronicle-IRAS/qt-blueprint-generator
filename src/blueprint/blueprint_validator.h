#pragma once

#include "blueprint/blueprint_document.h"

#include <QString>
#include <QVector>

struct BlueprintValidationContext
{
    QString projectRoot;
};

struct BlueprintDiagnostic
{
    QString code;
    QString nodeId;
    QString edgeId;
    QString message;
};

class BlueprintValidator final
{
public:
    static QVector<BlueprintDiagnostic> validate(
        const BlueprintDocument &document,
        const BlueprintValidationContext &context = {});

    BlueprintValidator() = delete;
};
