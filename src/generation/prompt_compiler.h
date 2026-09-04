#pragma once

#include <QJsonObject>
#include <QString>

#include <optional>

class PromptCompiler final
{
public:
    static QString compileProjectPrompt(const QJsonObject &ir);
    static std::optional<QString> compileModulePrompt(const QJsonObject &ir,
                                                      const QString &nodeId,
                                                      QString *errorMessage = nullptr);

    PromptCompiler() = delete;
};
