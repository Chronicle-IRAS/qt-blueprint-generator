#include <QtTest/QtTest>

#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>

#include "editor/node_properties_editor.h"
#include "blueprint/blueprint_serializer.h"

Q_DECLARE_METATYPE(NodeType)

namespace {

QWidget *section(NodePropertiesEditor &editor, const QString &name)
{
    QWidget *result = editor.findChild<QWidget *>(name);
    Q_ASSERT(result);
    return result;
}

QTableWidget *table(QWidget *section, const QString &name)
{
    QTableWidget *result = section->findChild<QTableWidget *>(name);
    Q_ASSERT(result);
    return result;
}

QPushButton *button(QWidget *section, const QString &name)
{
    QPushButton *result = section->findChild<QPushButton *>(name);
    Q_ASSERT(result);
    return result;
}

void setPortRow(QTableWidget *ports, int row, const PortSpec &port)
{
    QVERIFY(ports->item(row, 0));
    QVERIFY(ports->item(row, 1));
    QVERIFY(ports->item(row, 2));
    ports->item(row, 0)->setText(port.name);
    ports->item(row, 1)->setText(port.type);
    ports->item(row, 2)->setText(port.description);
}

} // namespace

class NodePropertiesEditorTest final : public QObject
{
    Q_OBJECT

private slots:
    void roundTripPreservesEditableFieldsWithoutChangingIdentity();
    void portCollectionsSupportAddEditAndDelete();
    void stringCollectionsSupportAddEditAndDelete();
    void emptyCollectionsAndRetranslationPreserveDrafts();
    void nodeTypeRowIsReadOnly_data();
    void nodeTypeRowIsReadOnly();
    void renameAndClearKeepTheReadOnlyType();
    void blueprintJsonSchemaKeepsTheNodeKeys();
};

void NodePropertiesEditorTest::roundTripPreservesEditableFieldsWithoutChangingIdentity()
{
    BlueprintNode source;
    source.id = QStringLiteral("source-id");
    source.type = NodeType::Decision;
    source.name = QStringLiteral("  条件节点  ");
    source.description = QStringLiteral("说明 \"quoted\" \\ path");
    source.inputs = {{QStringLiteral("用户名"), QStringLiteral("QVector<QString>"), QString()},
                     {QStringLiteral("用户名"), QStringLiteral("QString"), QStringLiteral("重复")}};
    source.outputs = {{QString(), QString(), QString()}};
    source.constraints = {QStringLiteral("  保留空白  "), QString(), QStringLiteral("重复"),
                          QStringLiteral("重复")};
    source.acceptanceCriteria = {QStringLiteral("[不是 JSON 语法要求]")};

    NodePropertiesEditor editor(QStringLiteral("test"));
    QCOMPARE(editor.objectName(), QStringLiteral("testNodePropertiesEditor"));
    editor.setNode(source);

    BlueprintNode updated;
    updated.id = QStringLiteral("destination-id");
    updated.type = NodeType::Start;
    editor.applyTo(&updated);

    QCOMPARE(updated.id, QStringLiteral("destination-id"));
    QCOMPARE(updated.type, NodeType::Start);
    QCOMPARE(updated.name, source.name);
    QCOMPARE(updated.description, source.description);
    QCOMPARE(updated.inputs, source.inputs);
    QCOMPARE(updated.outputs, source.outputs);
    QCOMPARE(updated.constraints, source.constraints);
    QCOMPARE(updated.acceptanceCriteria, source.acceptanceCriteria);

    BlueprintDocument document;
    document.projectId = QStringLiteral("structured-editor-round-trip");
    document.projectName = QStringLiteral("Structured Editor Round Trip");
    document.target = QStringLiteral("qt6-widgets-cpp17-cmake");
    document.nodes = {updated};
    QString error;
    const std::optional<BlueprintDocument> restored =
        BlueprintSerializer::fromJson(BlueprintSerializer::toJson(document), &error);
    QVERIFY2(restored.has_value(), qPrintable(error));
    QCOMPARE(restored.value(), document);
}

void NodePropertiesEditorTest::portCollectionsSupportAddEditAndDelete()
{
    NodePropertiesEditor editor(QStringLiteral("test"));
    QWidget *inputsSection = section(editor, QStringLiteral("testNodeInputsEditor"));
    QWidget *outputsSection = section(editor, QStringLiteral("testNodeOutputsEditor"));
    QTableWidget *inputs = table(inputsSection, QStringLiteral("portItemsTable"));
    QTableWidget *outputs = table(outputsSection, QStringLiteral("portItemsTable"));

    QTest::mouseClick(button(inputsSection, QStringLiteral("addPortButton")), Qt::LeftButton);
    QTest::mouseClick(button(inputsSection, QStringLiteral("addPortButton")), Qt::LeftButton);
    QCOMPARE(inputs->rowCount(), 2);
    setPortRow(inputs, 0, {QStringLiteral("first"), QStringLiteral("QString"), QStringLiteral("First")});
    setPortRow(inputs, 1, {QStringLiteral("second"), QStringLiteral("bool"), QStringLiteral("Second")});
    setPortRow(inputs, 0, {QStringLiteral("edited"), QStringLiteral("QVector<int>"), QStringLiteral("Changed")});
    auto *deleteSecond = qobject_cast<QPushButton *>(inputs->cellWidget(1, 3));
    QVERIFY(deleteSecond);
    QCOMPARE(deleteSecond->objectName(), QStringLiteral("deletePortButton"));
    QTest::mouseClick(deleteSecond, Qt::LeftButton);

    QTest::mouseClick(button(outputsSection, QStringLiteral("addPortButton")), Qt::LeftButton);
    setPortRow(outputs, 0, {QStringLiteral("result"), QStringLiteral("bool"), QStringLiteral("Result")});
    delete outputs->takeItem(0, 2);

    BlueprintNode result;
    editor.applyTo(&result);
    const QVector<PortSpec> expectedInputs{
        {QStringLiteral("edited"), QStringLiteral("QVector<int>"), QStringLiteral("Changed")}};
    const QVector<PortSpec> expectedOutputs{
        {QStringLiteral("result"), QStringLiteral("bool"), QString()}};
    QCOMPARE(result.inputs, expectedInputs);
    QCOMPARE(result.outputs, expectedOutputs);
}

void NodePropertiesEditorTest::stringCollectionsSupportAddEditAndDelete()
{
    NodePropertiesEditor editor(QStringLiteral("test"));
    QWidget *constraintsSection = section(editor, QStringLiteral("testNodeConstraintsEditor"));
    QWidget *criteriaSection = section(editor, QStringLiteral("testNodeAcceptanceCriteriaEditor"));
    QTableWidget *constraints = table(constraintsSection, QStringLiteral("stringItemsTable"));
    QTableWidget *criteria = table(criteriaSection, QStringLiteral("stringItemsTable"));

    QTest::mouseClick(button(constraintsSection, QStringLiteral("addStringItemButton")), Qt::LeftButton);
    QTest::mouseClick(button(constraintsSection, QStringLiteral("addStringItemButton")), Qt::LeftButton);
    constraints->item(0, 0)->setText(QStringLiteral("  原样保留  "));
    constraints->item(1, 0)->setText(QStringLiteral("删除我"));
    auto *deleteSecond = qobject_cast<QPushButton *>(constraints->cellWidget(1, 1));
    QVERIFY(deleteSecond);
    QCOMPARE(deleteSecond->objectName(), QStringLiteral("deleteStringItemButton"));
    QTest::mouseClick(deleteSecond, Qt::LeftButton);

    QTest::mouseClick(button(criteriaSection, QStringLiteral("addStringItemButton")), Qt::LeftButton);
    criteria->item(0, 0)->setText(QStringLiteral("\"quoted\" \\ path"));

    BlueprintNode result;
    editor.applyTo(&result);
    QCOMPARE(result.constraints, QStringList{QStringLiteral("  原样保留  ")});
    QCOMPARE(result.acceptanceCriteria, QStringList{QStringLiteral("\"quoted\" \\ path")});
}

void NodePropertiesEditorTest::emptyCollectionsAndRetranslationPreserveDrafts()
{
    NodePropertiesEditor editor(QStringLiteral("test"));
    BlueprintNode empty;
    empty.name = QStringLiteral("Draft name");
    editor.setNode(empty);
    QCOMPARE(table(section(editor, QStringLiteral("testNodeInputsEditor")),
                   QStringLiteral("portItemsTable"))->rowCount(), 0);
    QCOMPARE(table(section(editor, QStringLiteral("testNodeConstraintsEditor")),
                   QStringLiteral("stringItemsTable"))->rowCount(), 0);

    auto *name = editor.findChild<QLineEdit *>(QStringLiteral("testNodeNameEdit"));
    QVERIFY(name);
    name->setText(QStringLiteral("未提交草稿"));
    QWidget *constraintsSection = section(editor, QStringLiteral("testNodeConstraintsEditor"));
    QTest::mouseClick(button(constraintsSection, QStringLiteral("addStringItemButton")), Qt::LeftButton);
    table(constraintsSection, QStringLiteral("stringItemsTable"))->item(0, 0)->setText(QString());
    editor.retranslateUi();

    BlueprintNode result;
    editor.applyTo(&result);
    QCOMPARE(result.name, QStringLiteral("未提交草稿"));
    QCOMPARE(result.inputs, QVector<PortSpec>{});
    QCOMPARE(result.outputs, QVector<PortSpec>{});
    QCOMPARE(result.constraints, QStringList{QString()});
    QCOMPARE(result.acceptanceCriteria, QStringList{});
}

void NodePropertiesEditorTest::nodeTypeRowIsReadOnly_data()
{
    QTest::addColumn<NodeType>("type");
    QTest::addColumn<QString>("expectedText");

    QTest::newRow("Start") << NodeType::Start << QStringLiteral("Start");
    QTest::newRow("End") << NodeType::End << QStringLiteral("End");
    QTest::newRow("UiPage") << NodeType::UiPage << QStringLiteral("UI Page");
    QTest::newRow("LogicModule") << NodeType::LogicModule << QStringLiteral("Logic Module");
    QTest::newRow("Decision") << NodeType::Decision << QStringLiteral("Decision");
    QTest::newRow("ExternalCode") << NodeType::ExternalCode << QStringLiteral("External Code");
}

void NodePropertiesEditorTest::nodeTypeRowIsReadOnly()
{
    QFETCH(NodeType, type);
    QFETCH(QString, expectedText);

    NodePropertiesEditor editor(QStringLiteral("test"));
    BlueprintNode source;
    source.id = QStringLiteral("typed-node");
    source.type = type;
    source.name = QStringLiteral("Typed node");
    editor.setNode(source);

    auto *typeValue = qobject_cast<QLabel *>(
        editor.findChild<QLabel *>(QStringLiteral("testNodeTypeValue")));
    QVERIFY(typeValue);
    QCOMPARE(typeValue->text(), expectedText);
    QVERIFY(!(typeValue->textInteractionFlags() & Qt::TextEditable));

    const NodeType untouchedType =
        type == NodeType::Start ? NodeType::End : NodeType::Start;
    BlueprintNode target;
    target.id = QStringLiteral("target-node");
    target.type = untouchedType;
    editor.applyTo(&target);
    QCOMPARE(target.type, untouchedType);
    QCOMPARE(target.name, QStringLiteral("Typed node"));
}

void NodePropertiesEditorTest::renameAndClearKeepTheReadOnlyType()
{
    NodePropertiesEditor editor(QStringLiteral("test"));
    BlueprintNode source;
    source.id = QStringLiteral("login-id");
    source.type = NodeType::LogicModule;
    source.name = QStringLiteral("LoginService");
    editor.setNode(source);

    auto *typeValue = editor.findChild<QLabel *>(QStringLiteral("testNodeTypeValue"));
    auto *nameEdit = editor.findChild<QLineEdit *>(QStringLiteral("testNodeNameEdit"));
    QVERIFY(typeValue && nameEdit);
    QCOMPARE(typeValue->text(), QStringLiteral("Logic Module"));

    nameEdit->setText(QStringLiteral("MainWindow"));
    BlueprintNode target;
    target.type = NodeType::Decision;
    editor.applyTo(&target);
    QCOMPARE(target.name, QStringLiteral("MainWindow"));
    QCOMPARE(target.type, NodeType::Decision);
    QCOMPARE(typeValue->text(), QStringLiteral("Logic Module"));

    source.name = QStringLiteral("MainWindow");
    editor.setNode(source);
    QCOMPARE(nameEdit->text(), QStringLiteral("MainWindow"));
    QCOMPARE(typeValue->text(), QStringLiteral("Logic Module"));

    editor.clear();
    QCOMPARE(typeValue->text(), QString());
}

void NodePropertiesEditorTest::blueprintJsonSchemaKeepsTheNodeKeys()
{
    BlueprintDocument document;
    document.projectId = QStringLiteral("node-type-schema");
    document.projectName = QStringLiteral("Node Type Schema");
    document.target = QStringLiteral("qt6-widgets-cpp17-cmake");
    BlueprintNode node;
    node.id = QStringLiteral("login-id");
    node.type = NodeType::LogicModule;
    node.name = QStringLiteral("LoginService");
    node.description = QStringLiteral("Handles sign in");
    document.nodes = {node};

    const QByteArray json = BlueprintSerializer::toJson(document);
    QString error;
    const std::optional<BlueprintDocument> restored =
        BlueprintSerializer::fromJson(json, &error);
    QVERIFY2(restored.has_value(), qPrintable(error));
    QCOMPARE(restored.value().nodes.constFirst().type, NodeType::LogicModule);

    const QJsonDocument parsed = QJsonDocument::fromJson(json);
    QVERIFY(parsed.isObject());
    const QJsonArray nodes = parsed.object().value(QStringLiteral("nodes")).toArray();
    QCOMPARE(nodes.size(), 1);
    QStringList keys = nodes.at(0).toObject().keys();
    keys.sort();
    QCOMPARE(keys, QStringList({QStringLiteral("acceptanceCriteria"),
                                QStringLiteral("constraints"),
                                QStringLiteral("description"),
                                QStringLiteral("id"),
                                QStringLiteral("inputs"),
                                QStringLiteral("name"),
                                QStringLiteral("outputs"),
                                QStringLiteral("type")}));
}

QTEST_MAIN(NodePropertiesEditorTest)

#include "tst_node_properties_editor.moc"
