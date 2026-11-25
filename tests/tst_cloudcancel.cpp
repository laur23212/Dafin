// Dafin: cloud calls and cancel. A call that is told to stop while it waits out a rate limit must
// not send its retry. Real sockets on loopback, no provider account.

#include "app/online/CloudProviders.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

#include <atomic>
#include <thread>

namespace {

// Answers every request with the next status in `statuses` (the last one repeats).
class FakeProvider : public QObject
{
public:
    explicit FakeProvider(QList<int> statuses)
        : m_statuses(std::move(statuses))
    {
        m_listening = m_server.listen(QHostAddress::LocalHost);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    socket->readAll();
                    const int status = m_statuses.at(std::min<int>(m_requests, int(m_statuses.size()) - 1));
                    ++m_requests;
                    const QByteArray reason = status == 429 ? "Too Many Requests" : "OK";
                    socket->write("HTTP/1.1 " + QByteArray::number(status) + " " + reason
                                  + "\r\nRetry-After: 1\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}");
                    socket->disconnectFromHost();
                });
            }
        });
    }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/v1/user").arg(m_server.serverPort())); }
    int requests() const { return m_requests; }
    bool listening() const { return m_listening; }

private:
    QTcpServer m_server;
    bool m_listening = false;
    QList<int> m_statuses;
    std::atomic<int> m_requests{0};
};

} // namespace

class TestCloudCancel : public QObject
{
    Q_OBJECT

private slots:
    // Rate limited, then cancelled during the wait: the second request must never go out.
    void cancelDuringRateLimitWaitSendsNoRetry()
    {
        FakeProvider provider({429, 200});
        QVERIFY(provider.listening());
        std::atomic<bool> cancel{false};
        drift::cloud::CloudError error;
        std::atomic<int> status{-1};
        std::thread call([&] {
            status = drift::cloud::detail::getForTest(provider.url(), [&] { return cancel.load(); }, &error);
        });
        QTRY_COMPARE_WITH_TIMEOUT(provider.requests(), 1, 5000);
        cancel = true;
        // The wait is Retry-After (1 s); give a retry every chance to appear before joining.
        QTest::qWait(1800);
        call.join();
        QCOMPARE(provider.requests(), 1);
        QCOMPARE(error.code, QStringLiteral("cancelled"));
    }

    // Not cancelled: the one polite retry still happens and its answer is used.
    void rateLimitWithoutCancelRetriesOnce()
    {
        FakeProvider provider({429, 200});
        QVERIFY(provider.listening());
        drift::cloud::CloudError error;
        std::atomic<int> status{-1};
        std::thread call([&] { status = drift::cloud::detail::getForTest(provider.url(), {}, &error); });
        QTRY_COMPARE_WITH_TIMEOUT(status.load(), 200, 8000);
        call.join();
        QCOMPARE(provider.requests(), 2);
        QVERIFY(!error.isError());
    }
};

QTEST_MAIN(TestCloudCancel)
#include "tst_cloudcancel.moc"
