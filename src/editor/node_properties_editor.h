#pragma once

#include <QWidget>

#include "blueprint/blueprint_document.h"

class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

class NodePropertiesEditor final : public QWidget
{
    Q_OBJECT

public:
    explicit NodePropertiesEditor(const QString &objectNamePrefix, QWidget *parent = nullptr);

    void setNode(const BlueprintNode &node);
    void applyTo(BlueprintNode *node) const;
    void clear();
    void setEditorEnabled(bool enabled);
    void retranslateUi();

private:
    void appendPortRow(QTableWidget *table, const PortSpec &port);
    void appendStringRow(QTableWidget *table, const QString &value);
    static QVector<PortSpec> portsFrom(QTableWidget *table);
    static QStringList stringsFrom(QTableWidget *table);
    static void clearRows(QTableWidget *table);
    static void retranslateDeleteButtons(QTableWidget *table, int column,
                                         const QString &text);

    QLabel *m_nameLabel = nullptr;
    QLabel *m_descriptionLabel = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QPlainTextEdit *m_descriptionEdit = nullptr;
    QGroupBox *m_inputsGroup = nullptr;
    QTableWidget *m_inputsTable = nullptr;
    QPushButton *m_addInputButton = nullptr;
    QGroupBox *m_outputsGroup = nullptr;
    QTableWidget *m_outputsTable = nullptr;
    QPushButton *m_addOutputButton = nullptr;
    QGroupBox *m_constraintsGroup = nullptr;
    QTableWidget *m_constraintsTable = nullptr;
    QPushButton *m_addConstraintButton = nullptr;
    QGroupBox *m_acceptanceCriteriaGroup = nullptr;
    QTableWidget *m_acceptanceCriteriaTable = nullptr;
    QPushButton *m_addAcceptanceCriterionButton = nullptr;
};
