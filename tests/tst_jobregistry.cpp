// Dafin: what a job reports when cancel and success race. JobRegistry is the one place that decides
// it, for captions, Smart Shorts, voices and every MCP job. No GUI, no models.

#include "app/jobs/JobRegistry.h"

#include <QScopeGuard>
#include <QSemaphore>
#include <QtTest>

#include <memory>

namespace {

// Pumps the event loop until the job's `done` has run (the registry reports it finished).
QJsonObject waitForFinish(JobRegistry &registry, const QString &id)
{
    for (int i = 0; i < 500 && registry.job(id).value(QStringLiteral("active")).toBool(); ++i)
        QTest::qWait(10);
    return registry.job(id); // still "active": true here makes the caller's checks fail, not hang
}

} // namespace

class TestJobRegistry : public QObject
{
    Q_OBJECT

private slots:
    // The user pressed cancel while the work was running; it finished anyway and called succeed().
    // The job was cancelled: it must not report ok, and its result must not be handed to `done`
    // as a success.
    void cancelBeforeSucceedEndsCancelled()
    {
        JobRegistry registry;
        auto started = std::make_shared<QSemaphore>();
        auto go = std::make_shared<QSemaphore>();
        bool doneSawOk = true;
        const QString id = registry.start(
            QStringLiteral("test"), QStringLiteral("asset"), JobRegistry::Lane::Network,
            [started, go](JobContext &ctx) {
                started->release();
                go->acquire();
                ctx.succeed({{QStringLiteral("words"), 12}});
            },
            [&doneSawOk](const QJsonObject &job) {
                doneSawOk = job.value(QStringLiteral("ok")).toBool();
                return QJsonObject{};
            });
        started->acquire();
        // A failed check below must not leave the worker blocked: the registry joins it on exit.
        auto unblock = qScopeGuard([go] { go->release(); });
        QVERIFY(registry.cancel(id));
        unblock.dismiss();
        go->release();

        const QJsonObject job = waitForFinish(registry, id);
        QVERIFY(!job.value(QStringLiteral("ok")).toBool());
        QCOMPARE(job.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
                 QStringLiteral("cancelled"));
        QVERIFY(!job.contains(QStringLiteral("result")));
        QVERIFY(!doneSawOk);
    }

    // The work had already succeeded when cancel arrived. Nothing is left to stop, so cancel says
    // so, and the job stays a success.
    void cancelAfterSucceedIsRefused()
    {
        JobRegistry registry;
        auto succeeded = std::make_shared<QSemaphore>();
        auto release = std::make_shared<QSemaphore>();
        const QString id = registry.start(
            QStringLiteral("test"), QStringLiteral("asset"), JobRegistry::Lane::Network,
            [succeeded, release](JobContext &ctx) {
                ctx.succeed({{QStringLiteral("words"), 12}});
                succeeded->release();
                release->acquire();
            });
        succeeded->acquire();
        auto unblock = qScopeGuard([release] { release->release(); });
        QVERIFY(!registry.cancel(id));
        unblock.dismiss();
        release->release();

        const QJsonObject job = waitForFinish(registry, id);
        QVERIFY(job.value(QStringLiteral("ok")).toBool());
        QCOMPARE(job.value(QStringLiteral("result")).toObject().value(QStringLiteral("words")).toInt(), 12);
    }

    // A job that failed for its own reason before cancel was pressed keeps that reason.
    void failureKeepsItsCodeWhenCancelComesLater()
    {
        JobRegistry registry;
        auto failed = std::make_shared<QSemaphore>();
        auto release = std::make_shared<QSemaphore>();
        const QString id = registry.start(
            QStringLiteral("test"), QStringLiteral("asset"), JobRegistry::Lane::Network,
            [failed, release](JobContext &ctx) {
                ctx.fail(QStringLiteral("model_error"), QStringLiteral("boom"));
                failed->release();
                release->acquire();
            });
        failed->acquire();
        auto unblock = qScopeGuard([release] { release->release(); });
        QVERIFY(!registry.cancel(id));
        unblock.dismiss();
        release->release();

        const QJsonObject job = waitForFinish(registry, id);
        QVERIFY(!job.value(QStringLiteral("ok")).toBool());
        QCOMPARE(job.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
                 QStringLiteral("model_error"));
    }

    // Plain cases that must keep working: success untouched, cancel while running.
    void successWithoutCancelIsOk()
    {
        JobRegistry registry;
        const QString id = registry.start(QStringLiteral("test"), QStringLiteral("a"),
                                          JobRegistry::Lane::Network,
                                          [](JobContext &ctx) { ctx.succeed({{QStringLiteral("n"), 1}}); });
        const QJsonObject job = waitForFinish(registry, id);
        QVERIFY(job.value(QStringLiteral("ok")).toBool());
    }

    void cancelWhileWorkReturnsEarlyIsCancelled()
    {
        JobRegistry registry;
        auto started = std::make_shared<QSemaphore>();
        const QString id = registry.start(QStringLiteral("test"), QStringLiteral("a"),
                                          JobRegistry::Lane::Network, [started](JobContext &ctx) {
                                              started->release();
                                              while (!ctx.cancelled())
                                                  QThread::msleep(5);
                                          });
        started->acquire();
        QVERIFY(registry.cancel(id));
        const QJsonObject job = waitForFinish(registry, id);
        QVERIFY(!job.value(QStringLiteral("ok")).toBool());
        QCOMPARE(job.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
                 QStringLiteral("cancelled"));
    }
};

QTEST_MAIN(TestJobRegistry)
#include "tst_jobregistry.moc"
