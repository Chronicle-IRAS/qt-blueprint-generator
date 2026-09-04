#pragma once

#include <QMainWindow>
#include <QString>

#include "blueprint/blueprint_document.h"

class BlueprintScene;
class QGraphicsView;
class QLineEdit;
class QPlainTextEdit;

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(QWidget *parent = nullptr);

    bool addNodeOfType(NodeType type);
    const BlueprintDocument &document() const;
    BlueprintScene *scene() const;
    QGraphicsView *graphicsView() const;

private:
    void deleteSelection();
    void applyProperties();
    void updatePropertyEditor();
    QString selectedNodeId() const;

    BlueprintDocument m_document;
    BlueprintScene *m_scene = nullptr;
    QGraphicsView *m_view = nullptr;
    QLineEdit *m_connectionLabelEdit = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QPlainTextEdit *m_descriptionEdit = nullptr;
    QPlainTextEdit *m_inputsEdit = nullptr;
    QPlainTextEdit *m_outputsEdit = nullptr;
    QPlainTextEdit *m_constraintsEdit = nullptr;
    QPlainTextEdit *m_acceptanceCriteriaEdit = nullptr;
};
