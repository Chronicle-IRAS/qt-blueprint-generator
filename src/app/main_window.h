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

private:
    void addNode();
    void deleteSelection();
    void connectSelection();
    void applyProperties();
    void updatePropertyEditor();
    QString selectedNodeId() const;

    BlueprintDocument m_document;
    BlueprintScene *m_scene = nullptr;
    QGraphicsView *m_view = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QPlainTextEdit *m_descriptionEdit = nullptr;
};
