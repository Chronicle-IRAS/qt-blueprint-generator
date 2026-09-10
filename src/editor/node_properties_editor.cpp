#include "editor/node_properties_editor.h"

#include <QAbstractItemView>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

QString fieldPrefix(const QString &objectNamePrefix)
{
    return objectNamePrefix == QStringLiteral("inspector")
               ? QStringLiteral("node")
               : objectNamePrefix + QStringLiteral("Node");
}

QGroupBox *createGroup(const QString &objectName, QWidget *parent)
{
    auto *group = new QGroupBox(parent);
    group->setObjectName(objectName);
    return group;
}

QTableWidget *createTable(int columns, const QString &objectName, QWidget *parent)
{
    auto *table = new QTableWidget(0, columns, parent);
    table->setObjectName(objectName);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->setVisible(false);
    table->setMinimumHeight(120);
    return table;
}

QString itemText(QTableWidget *table, int row, int column)
{
    const QTableWidgetItem *item = table->item(row, column);
    return item ? item->text() : QString();
}

} // namespace

NodePropertiesEditor::NodePropertiesEditor(const QString &objectNamePrefix, QWidget *parent)
    : QWidget(parent)
{
    setObjectName(objectNamePrefix + QStringLiteral("NodePropertiesEditor"));

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_nameLabel = new QLabel(this);
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setObjectName(fieldPrefix(objectNamePrefix) + QStringLiteral("NameEdit"));
    form->addRow(m_nameLabel, m_nameEdit);

    m_descriptionLabel = new QLabel(this);
    m_descriptionEdit = new QPlainTextEdit(this);
    m_descriptionEdit->setObjectName(fieldPrefix(objectNamePrefix)
                                     + QStringLiteral("DescriptionEdit"));
    m_descriptionEdit->setMaximumHeight(100);
    form->addRow(m_descriptionLabel, m_descriptionEdit);
    layout->addLayout(form);

    m_inputsGroup = createGroup(objectNamePrefix + QStringLiteral("NodeInputsEditor"), this);
    auto *inputsLayout = new QVBoxLayout(m_inputsGroup);
    m_inputsTable = createTable(4, QStringLiteral("portItemsTable"), m_inputsGroup);
    m_inputsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_inputsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_inputsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_inputsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_addInputButton = new QPushButton(m_inputsGroup);
    m_addInputButton->setObjectName(QStringLiteral("addPortButton"));
    inputsLayout->addWidget(m_inputsTable);
    inputsLayout->addWidget(m_addInputButton);
    layout->addWidget(m_inputsGroup);

    m_outputsGroup = createGroup(objectNamePrefix + QStringLiteral("NodeOutputsEditor"), this);
    auto *outputsLayout = new QVBoxLayout(m_outputsGroup);
    m_outputsTable = createTable(4, QStringLiteral("portItemsTable"), m_outputsGroup);
    m_outputsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_outputsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_outputsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_outputsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_addOutputButton = new QPushButton(m_outputsGroup);
    m_addOutputButton->setObjectName(QStringLiteral("addPortButton"));
    outputsLayout->addWidget(m_outputsTable);
    outputsLayout->addWidget(m_addOutputButton);
    layout->addWidget(m_outputsGroup);

    m_constraintsGroup = createGroup(objectNamePrefix + QStringLiteral("NodeConstraintsEditor"), this);
    auto *constraintsLayout = new QVBoxLayout(m_constraintsGroup);
    m_constraintsTable = createTable(2, QStringLiteral("stringItemsTable"), m_constraintsGroup);
    m_constraintsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_constraintsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_addConstraintButton = new QPushButton(m_constraintsGroup);
    m_addConstraintButton->setObjectName(QStringLiteral("addStringItemButton"));
    constraintsLayout->addWidget(m_constraintsTable);
    constraintsLayout->addWidget(m_addConstraintButton);
    layout->addWidget(m_constraintsGroup);

    m_acceptanceCriteriaGroup = createGroup(
        objectNamePrefix + QStringLiteral("NodeAcceptanceCriteriaEditor"), this);
    auto *criteriaLayout = new QVBoxLayout(m_acceptanceCriteriaGroup);
    m_acceptanceCriteriaTable = createTable(2, QStringLiteral("stringItemsTable"),
                                            m_acceptanceCriteriaGroup);
    m_acceptanceCriteriaTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_acceptanceCriteriaTable->horizontalHeader()->setSectionResizeMode(1,
                                                                        QHeaderView::ResizeToContents);
    m_addAcceptanceCriterionButton = new QPushButton(m_acceptanceCriteriaGroup);
    m_addAcceptanceCriterionButton->setObjectName(QStringLiteral("addStringItemButton"));
    criteriaLayout->addWidget(m_acceptanceCriteriaTable);
    criteriaLayout->addWidget(m_addAcceptanceCriterionButton);
    layout->addWidget(m_acceptanceCriteriaGroup);
    layout->addStretch();

    connect(m_addInputButton, &QPushButton::clicked, this,
            [this] { appendPortRow(m_inputsTable, {}); });
    connect(m_addOutputButton, &QPushButton::clicked, this,
            [this] { appendPortRow(m_outputsTable, {}); });
    connect(m_addConstraintButton, &QPushButton::clicked, this,
            [this] { appendStringRow(m_constraintsTable, {}); });
    connect(m_addAcceptanceCriterionButton, &QPushButton::clicked, this,
            [this] { appendStringRow(m_acceptanceCriteriaTable, {}); });

    retranslateUi();
}

void NodePropertiesEditor::setNode(const BlueprintNode &node)
{
    m_nameEdit->setText(node.name);
    m_descriptionEdit->setPlainText(node.description);

    clearRows(m_inputsTable);
    for (const PortSpec &port : node.inputs) {
        appendPortRow(m_inputsTable, port);
    }
    clearRows(m_outputsTable);
    for (const PortSpec &port : node.outputs) {
        appendPortRow(m_outputsTable, port);
    }
    clearRows(m_constraintsTable);
    for (const QString &constraint : node.constraints) {
        appendStringRow(m_constraintsTable, constraint);
    }
    clearRows(m_acceptanceCriteriaTable);
    for (const QString &criterion : node.acceptanceCriteria) {
        appendStringRow(m_acceptanceCriteriaTable, criterion);
    }
}

void NodePropertiesEditor::applyTo(BlueprintNode *node) const
{
    if (!node) {
        return;
    }
    node->name = m_nameEdit->text();
    node->description = m_descriptionEdit->toPlainText();
    node->inputs = portsFrom(m_inputsTable);
    node->outputs = portsFrom(m_outputsTable);
    node->constraints = stringsFrom(m_constraintsTable);
    node->acceptanceCriteria = stringsFrom(m_acceptanceCriteriaTable);
}

void NodePropertiesEditor::clear()
{
    setNode({});
}

void NodePropertiesEditor::setEditorEnabled(bool enabled)
{
    setEnabled(enabled);
}

void NodePropertiesEditor::retranslateUi()
{
    m_nameLabel->setText(tr("Name"));
    m_descriptionLabel->setText(tr("Description"));

    const QStringList portHeaders{tr("Name"), tr("Type"), tr("Description"), tr("Delete")};
    m_inputsTable->setHorizontalHeaderLabels(portHeaders);
    m_outputsTable->setHorizontalHeaderLabels(portHeaders);
    const QStringList stringHeaders{tr("Text"), tr("Delete")};
    m_constraintsTable->setHorizontalHeaderLabels(stringHeaders);
    m_acceptanceCriteriaTable->setHorizontalHeaderLabels(stringHeaders);

    m_inputsGroup->setTitle(tr("Inputs"));
    m_outputsGroup->setTitle(tr("Outputs"));
    m_constraintsGroup->setTitle(tr("Constraints"));
    m_acceptanceCriteriaGroup->setTitle(tr("Acceptance Criteria"));
    m_addInputButton->setText(tr("Add input"));
    m_addOutputButton->setText(tr("Add output"));
    m_addConstraintButton->setText(tr("Add constraint"));
    m_addAcceptanceCriterionButton->setText(tr("Add criterion"));
    retranslateDeleteButtons(m_inputsTable, 3, tr("Delete"));
    retranslateDeleteButtons(m_outputsTable, 3, tr("Delete"));
    retranslateDeleteButtons(m_constraintsTable, 1, tr("Delete"));
    retranslateDeleteButtons(m_acceptanceCriteriaTable, 1, tr("Delete"));
}

void NodePropertiesEditor::appendPortRow(QTableWidget *table, const PortSpec &port)
{
    const int row = table->rowCount();
    table->insertRow(row);
    table->setItem(row, 0, new QTableWidgetItem(port.name));
    table->setItem(row, 1, new QTableWidgetItem(port.type));
    table->setItem(row, 2, new QTableWidgetItem(port.description));
    auto *deleteButton = new QPushButton(tr("Delete"), table);
    deleteButton->setObjectName(QStringLiteral("deletePortButton"));
    table->setCellWidget(row, 3, deleteButton);
    connect(deleteButton, &QPushButton::clicked, table, [table, deleteButton] {
        for (int currentRow = 0; currentRow < table->rowCount(); ++currentRow) {
            if (table->cellWidget(currentRow, 3) == deleteButton) {
                table->removeRow(currentRow);
                return;
            }
        }
    });
}

void NodePropertiesEditor::appendStringRow(QTableWidget *table, const QString &value)
{
    const int row = table->rowCount();
    table->insertRow(row);
    table->setItem(row, 0, new QTableWidgetItem(value));
    auto *deleteButton = new QPushButton(tr("Delete"), table);
    deleteButton->setObjectName(QStringLiteral("deleteStringItemButton"));
    table->setCellWidget(row, 1, deleteButton);
    connect(deleteButton, &QPushButton::clicked, table, [table, deleteButton] {
        for (int currentRow = 0; currentRow < table->rowCount(); ++currentRow) {
            if (table->cellWidget(currentRow, 1) == deleteButton) {
                table->removeRow(currentRow);
                return;
            }
        }
    });
}

QVector<PortSpec> NodePropertiesEditor::portsFrom(QTableWidget *table)
{
    QVector<PortSpec> ports;
    ports.reserve(table->rowCount());
    for (int row = 0; row < table->rowCount(); ++row) {
        ports.append({itemText(table, row, 0), itemText(table, row, 1),
                      itemText(table, row, 2)});
    }
    return ports;
}

QStringList NodePropertiesEditor::stringsFrom(QTableWidget *table)
{
    QStringList values;
    values.reserve(table->rowCount());
    for (int row = 0; row < table->rowCount(); ++row) {
        values.append(itemText(table, row, 0));
    }
    return values;
}

void NodePropertiesEditor::clearRows(QTableWidget *table)
{
    table->setRowCount(0);
}

void NodePropertiesEditor::retranslateDeleteButtons(QTableWidget *table, int column,
                                                     const QString &text)
{
    for (int row = 0; row < table->rowCount(); ++row) {
        if (auto *button = qobject_cast<QPushButton *>(table->cellWidget(row, column))) {
            button->setText(text);
        }
    }
}
