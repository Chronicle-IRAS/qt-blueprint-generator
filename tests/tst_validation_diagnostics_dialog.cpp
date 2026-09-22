#include <QtTest/QtTest>

#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTranslator>

#include "app/validation_diagnostics_dialog.h"

namespace {

BlueprintDiagnostic diagnostic(const QString &code, const QString &nodeId,
                               const QString &edgeId, const QString &message)
{
    BlueprintDiagnostic result;
    result.code = code;
    result.nodeId = nodeId;
    result.edgeId = edgeId;
    result.message = message;
    return result;
}

} // namespace

class ValidationDiagnosticsDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void showsEveryDiagnosticWithItsIdentifiers();
    void showsAMessageWithoutIdentifiers();
    void followsTheLanguageSwitch();
};

void ValidationDiagnosticsDialogTest::showsEveryDiagnosticWithItsIdentifiers()
{
    const BlueprintDiagnostic first = diagnostic(
        QStringLiteral("node.unreachable"), QStringLiteral("node-4"), {},
        QStringLiteral("Node is not reachable from Start"));
    const BlueprintDiagnostic second = diagnostic(
        QStringLiteral("edge.source.missing"), {}, QStringLiteral("edge-7"),
        QStringLiteral("Edge source 'ghost' does not exist"));

    ValidationDiagnosticsDialog dialog({first, second});

    QCOMPARE(dialog.objectName(), QStringLiteral("validationDiagnosticsDialog"));
    auto *details = dialog.findChild<QPlainTextEdit *>(QStringLiteral("validationDiagnosticsText"));
    QVERIFY(details);
    QVERIFY(details->isReadOnly());
    const QString text = details->toPlainText();
    QVERIFY(text.contains(QStringLiteral("node: node-4")));
    QVERIFY(text.contains(QStringLiteral("Node is not reachable from Start")));
    QVERIFY(text.contains(QStringLiteral("edge: edge-7")));
    QVERIFY(text.contains(QStringLiteral("Edge source 'ghost' does not exist")));
    // Both diagnostics are reported at once instead of only the first one.
    QCOMPARE(text,
             QStringLiteral("node: node-4\nNode is not reachable from Start\n\n"
                            "edge: edge-7\nEdge source 'ghost' does not exist"));
}

void ValidationDiagnosticsDialogTest::showsAMessageWithoutIdentifiers()
{
    const BlueprintDiagnostic onlyMessage = diagnostic(
        QStringLiteral("graph.start.missing"), {}, {},
        QStringLiteral("The graph must contain exactly one Start node"));

    ValidationDiagnosticsDialog dialog({onlyMessage});

    QCOMPARE(ValidationDiagnosticsDialog::describe(onlyMessage),
             QStringLiteral("The graph must contain exactly one Start node"));
    auto *details = dialog.findChild<QPlainTextEdit *>(QStringLiteral("validationDiagnosticsText"));
    QVERIFY(details);
    QCOMPARE(details->toPlainText(),
             QStringLiteral("The graph must contain exactly one Start node"));
    QVERIFY(!details->toPlainText().contains(QStringLiteral("node:")));
    QVERIFY(!details->toPlainText().contains(QStringLiteral("edge:")));
}

void ValidationDiagnosticsDialogTest::followsTheLanguageSwitch()
{
    QTranslator translator;
    QVERIFY(translator.load(QStringLiteral(":/i18n/BlueprintEditor_zh_CN.qm")));

    ValidationDiagnosticsDialog dialog({diagnostic(QStringLiteral("node.unreachable"),
                                                   QStringLiteral("node-4"), {},
                                                   QStringLiteral("Node is not reachable from Start")),
                                        diagnostic(QStringLiteral("edge.source.missing"), {},
                                                   QStringLiteral("edge-7"),
                                                   QStringLiteral("Edge source 'ghost' does not exist"))});
    auto *summary = dialog.findChild<QLabel *>(QStringLiteral("validationDiagnosticsSummary"));
    auto *close = dialog.findChild<QPushButton *>(QStringLiteral("closeValidationDiagnosticsButton"));
    auto *details = dialog.findChild<QPlainTextEdit *>(QStringLiteral("validationDiagnosticsText"));
    QVERIFY(summary && close && details);
    QCOMPARE(dialog.windowTitle(), QStringLiteral("Blueprint validation failed"));
    QCOMPARE(close->text(), QStringLiteral("Close"));
    QVERIFY(summary->text().contains(QStringLiteral("Fix these blueprint problems")));
    QVERIFY(details->toPlainText().contains(QStringLiteral("node: node-4")));

    qApp->installTranslator(&translator);
    QTRY_COMPARE(dialog.windowTitle(), QStringLiteral("蓝图校验失败"));
    QCOMPARE(close->text(), QStringLiteral("关闭"));
    QTRY_VERIFY(summary->text().contains(QStringLiteral("生成代码前请先修正")));
    // The identifier labels follow the language as well; the validator messages stay
    // exactly as the validator produced them.
    QTRY_VERIFY(details->toPlainText().contains(QStringLiteral("节点：node-4")));
    QVERIFY(details->toPlainText().contains(QStringLiteral("连线：edge-7")));
    QVERIFY(details->toPlainText().contains(QStringLiteral("Node is not reachable from Start")));
    qApp->removeTranslator(&translator);
}

QTEST_MAIN(ValidationDiagnosticsDialogTest)

#include "tst_validation_diagnostics_dialog.moc"
