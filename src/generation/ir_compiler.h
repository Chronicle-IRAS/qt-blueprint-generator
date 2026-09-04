#pragma once

#include "blueprint/blueprint_document.h"

#include <QByteArray>
#include <QJsonObject>

class IrCompiler final
{
public:
    static QJsonObject compile(const BlueprintDocument &document);
    static QByteArray toCanonicalJson(const QJsonObject &ir);

    IrCompiler() = delete;
};
