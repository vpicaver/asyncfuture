#include <QTest>
#include <Automator>
#include <QtConcurrent>
#include <asyncfuture.h>
#include "testfunctions.h"
#include "shieldtests.h"

using namespace AsyncFuture;
using namespace Test;

ShieldTests::ShieldTests(QObject *parent) : QObject(parent)
{
    // This function do nothing but could make Qt Creator Autotests plugin recognize this test
    auto ref =[this]() {
        QTest::qExec(this, 0, 0);
    };
    Q_UNUSED(ref);
}

void ShieldTests::test_shield_forwards_result()
{
    auto defer = deferred<int>();
    auto shielded = shield(defer.future());

    QVERIFY(!shielded.isFinished());

    defer.complete(42);

    QVERIFY(waitUntil(shielded, 1000));
    QVERIFY(!shielded.isCanceled());
    QCOMPARE(shielded.result(), 42);
}

void ShieldTests::test_shield_forwards_upstream_cancel()
{
    auto defer = deferred<int>();
    auto shielded = shield(defer.future());

    defer.cancel();

    QVERIFY(waitUntil([=]() {
        return shielded.isCanceled();
    }, 1000));
}

void ShieldTests::test_shield_blocks_downstream_cancel()
{
    auto defer = deferred<int>();
    auto inner = defer.future();

    Callable<int> directObserver;
    observe(inner).subscribe(directObserver.func);

    auto shielded = shield(inner);
    shielded.cancel();
    tick();

    // The barrier: the consumer's cancel does not reach the inner future.
    QVERIFY(shielded.isCanceled());
    QVERIFY(!inner.isCanceled());

    // The inner deferred can still complete, and a direct observer of
    // the inner future still fires.
    defer.complete(7);

    QVERIFY(waitUntil([&]() {
        return directObserver.called;
    }, 1000));
    QCOMPARE(directObserver.value, 7);
    QVERIFY(!inner.isCanceled());
    QVERIFY(shielded.isCanceled());
}

void ShieldTests::test_shield_shared_fanout()
{
    // Miniature of the shared-drain-future hazard: two consumers each
    // observe their own shield of the same deferred's future. One
    // consumer canceling must not starve the other.
    auto shared = deferred<void>();

    auto shielded1 = shield(shared.future());
    auto shielded2 = shield(shared.future());

    Callable<void> consumer2;
    observe(shielded2).subscribe(consumer2.func);

    shielded1.cancel();
    tick();

    QVERIFY(!shared.future().isCanceled());

    shared.complete();

    QVERIFY(waitUntil([&]() {
        return consumer2.called;
    }, 1000));
    QVERIFY(shielded2.isFinished());
    QVERIFY(!shielded2.isCanceled());
}

void ShieldTests::test_shield_forwards_progress()
{
    auto defer = deferred<int>();
    defer.setProgressRange(0, 10);

    auto shielded = shield(defer.future());

    defer.setProgressValue(5);

    QVERIFY(waitUntil([=]() {
        return shielded.progressMaximum() == 10 && shielded.progressValue() == 5;
    }, 1000));

    defer.complete(1);
    QVERIFY(waitUntil(shielded, 1000));
}

void ShieldTests::test_shield_thread_hop()
{
    auto inner = QtConcurrent::run([]() {
        Automator::wait(50);
        return 99;
    });

    auto shielded = shield(inner);

    bool onMainThread = false;
    Callable<int> observer;
    observe(shielded).subscribe([&](int value) {
        onMainThread = QThread::currentThread() == QCoreApplication::instance()->thread();
        observer.func(value);
    });

    QVERIFY(waitUntil([&]() {
        return observer.called;
    }, 5000));
    QCOMPARE(observer.value, 99);
    QVERIFY(onMainThread);
}

void ShieldTests::test_complete_policy_parity()
{
    // Both policies behave identically except for the downstream-cancel
    // push, exercised through Deferred::complete(QFuture, policy).
    const auto policies = {CancelPropagation::Propagate, CancelPropagation::Blocked};

    for (auto policy : policies) {
        // Value forwarding
        {
            auto inner = deferred<int>();
            auto outer = deferred<int>();
            outer.complete(inner.future(), policy);
            auto future = outer.future();

            inner.complete(3);

            QVERIFY(waitUntil(future, 1000));
            QVERIFY(!future.isCanceled());
            QCOMPARE(future.result(), 3);
        }

        // Upstream cancel forwarding
        {
            auto inner = deferred<int>();
            auto outer = deferred<int>();
            outer.complete(inner.future(), policy);
            auto future = outer.future();

            inner.cancel();

            QVERIFY(waitUntil([=]() {
                return future.isCanceled();
            }, 1000));
        }

        // Downstream cancel: pushed upstream only under Propagate
        {
            auto inner = deferred<int>();
            auto outer = deferred<int>();
            outer.complete(inner.future(), policy);
            auto future = outer.future();

            future.cancel();

            if (policy == CancelPropagation::Propagate) {
                QVERIFY(waitUntil([&]() {
                    return inner.future().isCanceled();
                }, 1000));
            } else {
                tick();
                QVERIFY(!inner.future().isCanceled());
            }
        }
    }
}
