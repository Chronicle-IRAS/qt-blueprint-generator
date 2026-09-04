#pragma once

#include "blueprint/blueprint_document.h"

#include <QByteArray>
#include <QString>

#include <optional>

class BlueprintSerializer final
{
public:
    static QByteArray toJson(const BlueprintDocument &document);
    static std::optional<BlueprintDocument> fromJson(const QByteArray &json,
                                                     QString *errorMessage = nullptr);

    BlueprintSerializer() = delete;
};
