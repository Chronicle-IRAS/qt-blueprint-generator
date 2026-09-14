#include "ai/ai_connection_tester.h"
#include "ai/fake_ai_client.h"

#include <QPointer>
#include <QSignalSpy>
#include <QtTest>

class AiConnectionTesterTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void sendsLowCostProbeAndReportsSuccess();
    void mapsMatchingFailureAndIgnoresUnrelatedIds();
    void completesEachAttemptOnlyOnceAndAllowsRetry();
    void rejectsDuplicateStartWhileTesting();
    void failsOnceWhenClientIsDestroyed();
    void nullClientFailsAsynchronouslyOnce();
    void deletionDuringTestingNotificationIsSafe();
    void testerMayBeDestroyedWithQueuedClientCompletion();
};

void AiConnectionTesterTest::initTestCase()
{
    qRegisterMetaType<AiConnectionState>();
    qRegisterMetaType<AiClientError>();
}

void AiConnectionTesterTest::sendsLowCostProbeAndReportsSuccess()
{
    FakeAiClient client;
    client.setSuccessfulResponse(QByteArray("ok"));
    AiConnectionTester tester(&client);
    QSignalSpy stateSpy(&tester, &AiConnectionTester::stateChanged);

    tester.testConnection();

    QCOMPARE(stateSpy.count(), 1);
    QCOMPARE(qvariant_cast<AiConnectionState>(stateSpy.at(0).at(0)),
             AiConnectionState::Testing);
    QCOMPARE(client.requests().size(), 1);
    const AiRequest request = client.requests().constFirst();
    QVERIFY(!request.requestId.isNull());
    QCOMPARE(request.maxTokens, std::optional<int>(8));
    QCOMPARE(request.disableThinking, std::optional<bool>(true));
    QVERIFY(request.maxWireResponseBytes > 0);
    QVERIFY(stateSpy.wait(500));
    QCOMPARE(stateSpy.count(), 2);
    QCOMPARE(qvariant_cast<AiConnectionState>(stateSpy.at(1).at(0)),
             AiConnectionState::Success);
}

void AiConnectionTesterTest::mapsMatchingFailureAndIgnoresUnrelatedIds()
{
    FakeAiClient client;
    AiConnectionTester tester(&client);
    QSignalSpy stateSpy(&tester, &AiConnectionTester::stateChanged);
    const AiClientError expected{AiErrorKind::Authentication,
                                 401,
                                 QStringLiteral("AI provider authentication failed")};

    tester.testConnection();
    QCOMPARE(client.requests().size(), 1);
    const QUuid requestId = client.requests().constFirst().requestId;
    emit client.responseReady(QUuid::createUuid(), QByteArray("unrelated"));
    emit client.requestFailed(QUuid::createUuid(),
                              {AiErrorKind::Unknown, 0, QStringLiteral("unrelated")});
    QCOMPARE(stateSpy.count(), 1);
    emit client.requestFailed(requestId, expected);

    QCOMPARE(stateSpy.count(), 2);
    QCOMPARE(qvariant_cast<AiConnectionState>(stateSpy.at(1).at(0)),
             AiConnectionState::Failure);
    const AiClientError actual = qvariant_cast<AiClientError>(stateSpy.at(1).at(1));
    QCOMPARE(actual.kind, expected.kind);
    QCOMPARE(actual.httpStatus, expected.httpStatus);
    QCOMPARE(actual.safeMessage, expected.safeMessage);
}

void AiConnectionTesterTest::completesEachAttemptOnlyOnceAndAllowsRetry()
{
    FakeAiClient client;
    AiConnectionTester tester(&client);
    QSignalSpy stateSpy(&tester, &AiConnectionTester::stateChanged);

    tester.testConnection();
    const QUuid firstId = client.requests().constFirst().requestId;
    emit client.responseReady(firstId, QByteArray("ok"));
    emit client.requestFailed(firstId,
                              {AiErrorKind::Unknown, 0, QStringLiteral("late failure")});
    QCOMPARE(stateSpy.count(), 2);

    tester.testConnection();
    QCOMPARE(client.requests().size(), 2);
    const QUuid secondId = client.requests().at(1).requestId;
    QVERIFY(secondId != firstId);
    emit client.responseReady(secondId, QByteArray("ok"));
    QCOMPARE(stateSpy.count(), 4);
    QCOMPARE(qvariant_cast<AiConnectionState>(stateSpy.at(2).at(0)),
             AiConnectionState::Testing);
    QCOMPARE(qvariant_cast<AiConnectionState>(stateSpy.at(3).at(0)),
             AiConnectionState::Success);
}

void AiConnectionTesterTest::rejectsDuplicateStartWhileTesting()
{
    FakeAiClient client;
    AiConnectionTester tester(&client);
    QSignalSpy stateSpy(&tester, &AiConnectionTester::stateChanged);

    tester.testConnection();
    tester.testConnection();

    QCOMPARE(client.requests().size(), 1);
    QCOMPARE(stateSpy.count(), 1);
}

void AiConnectionTesterTest::failsOnceWhenClientIsDestroyed()
{
    auto *client = new FakeAiClient;
    AiConnectionTester tester(client);
    QSignalSpy stateSpy(&tester, &AiConnectionTester::stateChanged);

    tester.testConnection();
    delete client;

    QTRY_COMPARE_WITH_TIMEOUT(stateSpy.count(), 2, 500);
    QCOMPARE(qvariant_cast<AiConnectionState>(stateSpy.at(1).at(0)),
             AiConnectionState::Failure);
    const AiClientError error = qvariant_cast<AiClientError>(stateSpy.at(1).at(1));
    QCOMPARE(error.kind, AiErrorKind::Network);
    QVERIFY(!error.safeMessage.isEmpty());
    QTest::qWait(25);
    QCOMPARE(stateSpy.count(), 2);
}

void AiConnectionTesterTest::nullClientFailsAsynchronouslyOnce()
{
    AiConnectionTester tester(nullptr);
    QSignalSpy stateSpy(&tester, &AiConnectionTester::stateChanged);

    tester.testConnection();

    QCOMPARE(stateSpy.count(), 1);
    QCOMPARE(qvariant_cast<AiConnectionState>(stateSpy.at(0).at(0)),
             AiConnectionState::Testing);
    QVERIFY(stateSpy.wait(500));
    QCOMPARE(stateSpy.count(), 2);
    QCOMPARE(qvariant_cast<AiConnectionState>(stateSpy.at(1).at(0)),
             AiConnectionState::Failure);
    QCOMPARE(qvariant_cast<AiClientError>(stateSpy.at(1).at(1)).kind,
             AiErrorKind::Network);
    QTest::qWait(25);
    QCOMPARE(stateSpy.count(), 2);
}

void AiConnectionTesterTest::deletionDuringTestingNotificationIsSafe()
{
    FakeAiClient client;
    auto *tester = new AiConnectionTester(&client);
    QPointer<AiConnectionTester> guard(tester);
    int testingCount = 0;
    QObject::connect(tester,
                     &AiConnectionTester::stateChanged,
                     tester,
                     [tester, &testingCount](AiConnectionState state, const AiClientError &) {
                         if (state == AiConnectionState::Testing) {
                             ++testingCount;
                             delete tester;
                         }
                     });

    tester->testConnection();

    QCOMPARE(testingCount, 1);
    QVERIFY(guard.isNull());
    QCOMPARE(client.requests().size(), 0);
}

void AiConnectionTesterTest::testerMayBeDestroyedWithQueuedClientCompletion()
{
    FakeAiClient client;
    client.setSuccessfulResponse(QByteArray("ok"));
    auto *tester = new AiConnectionTester(&client);
    QPointer<AiConnectionTester> guard(tester);

    tester->testConnection();
    delete tester;

    QVERIFY(guard.isNull());
    QTest::qWait(25);
}

QTEST_GUILESS_MAIN(AiConnectionTesterTest)

#include "tst_ai_connection_tester.moc"
