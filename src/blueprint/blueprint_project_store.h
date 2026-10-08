#pragma once

#include "blueprint/blueprint_document.h"

#include <QHash>
#include <QPointF>
#include <QString>

#include <optional>

struct BlueprintProject
{
    BlueprintDocument document;
    QHash<QString, QPointF> layout;
};

class BlueprintProjectStore final
{
public:
    static std::optional<BlueprintProject> load(const QString &projectDirectory,
                                              QString *errorMessage = nullptr);
    static bool save(const QString &projectDirectory,
                     const BlueprintDocument &document,
                     const QHash<QString, QPointF> &layout,
                     QString *errorMessage = nullptr);
    static bool validate(const BlueprintDocument &document,
                         const QHash<QString, QPointF> &layout,
                         QString *errorMessage = nullptr);

    BlueprintProjectStore() = delete;
};
