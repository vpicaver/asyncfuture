#include <QTest>
#include <Automator>
#include <QtConcurrent>
#include <asyncfuture.h>
#include "testfunctions.h"
#include "settledtests.h"

using namespace AsyncFuture;
using namespace Test;

namespace {

    // A future that is started, finished and carries progress - what a real
    // completed job looks like, rather than a bare reportFinished().
    QFuture<void> settledVoid() {
        QFutureInterface<void> fi;
        fi.reportStarted();
        fi.setProgressRange(0, 1);
        fi.setProgressValue(1);
        fi.reportFinished();
        return fi.future();
    }

    QFuture<void> canceledVoid() {
        auto defer = deferred<void>();
        defer.cancel();
        return defer.future();
    }

    // A job that ran, made progress and was then canceled. canceledVoid() was
    // never started and carries no progress, so it cannot see whether the
    // canceled branch mirrors the input's state.
    QFuture<void> canceledAfterProgress() {
        QFutureInterface<void> fi;
        fi.reportStarted();
        fi.setProgressRange(0, 10);
        fi.setProgressValue(4);
        fi.reportCanceled();
        fi.reportFinished();
        return fi.future();
    }

}

SettledTests::SettledTests(QObject *parent) : QObject(parent)
{
    // This function do nothing but could make Qt Creator Autotests plugin recognize this test
    auto ref =[this]() {
        QTest::qExec(this, 0, 0);
    };
    Q_UNUSED(ref);
}

void SettledTests::test_complete_settled_future_is_immediate()
{
    auto defer = deferred<void>();
    defer.complete(settledVoid());

    // No Automator::wait() in between: the deferred settles in the same turn
    // instead of waiting on a QFutureWatcher.
    QVERIFY(defer.future().isFinished());
    QVERIFY(!defer.future().isCanceled());
}

void SettledTests::test_complete_settled_future_keeps_result()
{
    auto defer = deferred<int>();
    defer.complete(completed(42));

    QVERIFY(defer.future().isFinished());
    QCOMPARE(defer.future().result(), 42);
}

void SettledTests::test_complete_settled_future_keeps_progress()
{
    auto defer = deferred<void>();
    defer.complete(settledVoid());

    QCOMPARE(defer.future().progressMaximum(), 1);
    QCOMPARE(defer.future().progressValue(), 1);
    QVERIFY(defer.future().isStarted());
}

void SettledTests::test_complete_settled_future_observer_is_async()
{
    auto defer = deferred<void>();
    defer.complete(settledVoid());

    Callable<void> observer;
    observe(defer.future()).context(this, observer.func);

    // The contract the fast path must not break: only the deferred's own state
    // moves early. User callbacks still arrive through the event loop.
    QVERIFY(!observer.called);
    QVERIFY(waitUntil([&]() {
        return observer.called;
    }, 1000));
}

void SettledTests::test_complete_canceled_future_is_immediate()
{
    auto defer = deferred<void>();
    defer.complete(canceledVoid());

    QVERIFY(defer.future().isCanceled());
    QVERIFY(defer.future().isFinished());
}

void SettledTests::test_complete_canceled_future_keeps_progress()
{
    auto defer = deferred<void>();
    defer.complete(canceledAfterProgress());

    // track() mirrors progress and started state for canceled inputs too, so the
    // canceled branch of the fast path must not settle without it. Otherwise a
    // progress bar bound to the deferred snaps to 0/0 instead of holding 4/10.
    QVERIFY(defer.future().isCanceled());
    QVERIFY(defer.future().isStarted());
    QCOMPARE(defer.future().progressMaximum(), 10);
    QCOMPARE(defer.future().progressValue(), 4);
}

void SettledTests::test_complete_settled_then_cancel_stays_completed()
{
    auto defer = deferred<int>();
    defer.complete(completed(7));
    defer.cancel();

    // Deferred::cancel() is a no-op once the future has settled - a retroactive
    // cancel would make every observer downstream drop its callback. Before the
    // fast path this sequence slipped past that guard, but only when the event
    // loop had not run in between, which made it a race rather than a rule.
    QVERIFY(!defer.future().isCanceled());
    QVERIFY(defer.future().isFinished());
    QCOMPARE(defer.future().result(), 7);
}

void SettledTests::test_complete_settled_then_cancel_is_not_timing_dependent()
{
    auto defer = deferred<int>();
    defer.complete(completed(7));
    tick();
    defer.cancel();

    // Same outcome as without the tick(). That is the point: the two differed
    // before the fast path.
    QVERIFY(!defer.future().isCanceled());
    QVERIFY(defer.future().isFinished());
    QCOMPARE(defer.future().result(), 7);
}

void SettledTests::test_complete_settled_future_handle_stays_cancelable()
{
    auto defer = deferred<int>();
    defer.complete(completed(7));

    auto future = defer.future();
    future.cancel();

    // Canceling the QFuture handle is Qt's own semantics, where a finished
    // future can still be marked canceled. The fast path must not change it.
    QVERIFY(future.isCanceled());
    QVERIFY(future.isFinished());
}

void SettledTests::test_complete_settled_future_does_not_propagate_cancel()
{
    auto input = settledVoid();

    auto defer = deferred<void>();
    defer.complete(input);

    auto future = defer.future();
    future.cancel();
    tick();

    // Qt's cancel() sets the flag even on a finished future, so propagating here
    // would mark completed upstream work canceled for every other observer of a
    // shared input - the hazard shield() exists to prevent. There is nothing left
    // to stop in a finished input, so Propagate has nothing to push back.
    QVERIFY(!input.isCanceled());
    QVERIFY(input.isFinished());
}

void SettledTests::test_complete_settled_future_off_thread()
{
    auto defer = deferred<int>();
    auto future = defer.future();

    // The fast path is gated on the main thread, so this goes through the
    // watcher. The observable result must be identical either way.
    auto worker = QtConcurrent::run([defer]() mutable {
        defer.complete(completed(13));
    });

    QVERIFY(waitUntil(worker, 1000));
    QVERIFY(waitUntil(future, 1000));
    QVERIFY(!future.isCanceled());
    QCOMPARE(future.result(), 13);
}

void SettledTests::test_combinator_stream_still_defers()
{
    auto combinator = combine();
    combinator << settledVoid() << settledVoid();
    auto future = combinator.future();

    // combine() << f stays open for more inputs, so it may not settle inline
    // even when every input so far has finished.
    QVERIFY(!future.isFinished());
    QVERIFY(waitUntil(future, 1000));
    QVERIFY(!future.isCanceled());
}

void SettledTests::test_combinator_stream_is_not_premature()
{
    auto pending = deferred<void>();

    auto combinator = combine();
    combinator << settledVoid() << pending.future();
    auto future = combinator.future();

    tick();
    QVERIFY(!future.isFinished());

    pending.complete();
    QVERIFY(waitUntil(future, 1000));
    QVERIFY(!future.isCanceled());
}

void SettledTests::test_combine_set_is_immediate()
{
    auto joined = combine({settledVoid(), settledVoid()});

    QVERIFY(joined.future().isFinished());
    QVERIFY(!joined.future().isCanceled());
}

void SettledTests::test_combine_set_waits_for_pending()
{
    auto pending = deferred<void>();
    auto joined = combine({settledVoid(), pending.future()});

    QVERIFY(!joined.future().isFinished());
    tick();
    QVERIFY(!joined.future().isFinished());

    pending.complete();
    QVERIFY(waitUntil(joined.future(), 1000));
    QVERIFY(!joined.future().isCanceled());
}

void SettledTests::test_combine_set_fail_fast_cancels()
{
    auto pending = deferred<void>();
    auto joined = combine({settledVoid(), pending.future()});

    QVERIFY(!joined.future().isFinished());

    pending.cancel();
    QVERIFY(waitUntil([&]() {
        return joined.future().isCanceled();
    }, 1000));
}

void SettledTests::test_combine_set_canceled_input_is_immediate()
{
    auto joined = combine({settledVoid(), canceledVoid()});

    QVERIFY(joined.future().isCanceled());
    QVERIFY(joined.future().isFinished());
}

void SettledTests::test_combine_set_all_settled_completes()
{
    auto joined = combine({settledVoid(), settledVoid()}, AllSettled);

    QVERIFY(joined.future().isFinished());
    QVERIFY(!joined.future().isCanceled());
}

void SettledTests::test_combine_set_observer_is_async()
{
    auto joined = combine({settledVoid(), settledVoid()});

    Callable<void> observer;
    joined.context(this, observer.func);

    QVERIFY(!observer.called);
    QVERIFY(waitUntil([&]() {
        return observer.called;
    }, 1000));
}

void SettledTests::test_combine_list_is_immediate()
{
    QList<QFuture<void>> futures;
    futures << settledVoid() << settledVoid() << settledVoid();

    auto joined = combine(futures);

    QVERIFY(joined.future().isFinished());
    QVERIFY(!joined.future().isCanceled());
}

void SettledTests::test_combine_list_of_typed_futures()
{
    QList<QFuture<int>> futures;
    futures << completed(1) << completed(2);

    auto joined = combine(futures);

    QVERIFY(joined.future().isFinished());
    QVERIFY(!joined.future().isCanceled());
}

void SettledTests::test_combine_empty_set_completes()
{
    QList<QFuture<void>> none;
    auto joined = combine(none);

    // Deliberately different from combine() << QList<QFuture<void>>(), which
    // cancels. Joining nothing is a satisfied precondition, and canceling would
    // poison every node downstream of an empty dependency set.
    QVERIFY(joined.future().isFinished());
    QVERIFY(!joined.future().isCanceled());
}

void SettledTests::test_combine_set_keeps_settled_progress()
{
    auto pending = deferred<void>();
    auto joined = combine({settledVoid(), settledVoid(), pending.future()});

    // Each addFuture() widens the range, and Qt's setProgressRange() resets the
    // reported value, so without a restore the two settled inputs contribute 0
    // and the join reads 0/3 for as long as the pending input runs.
    QCOMPARE(joined.future().progressMaximum(), 3);
    QCOMPARE(joined.future().progressValue(), 2);

    tick();
    QCOMPARE(joined.future().progressValue(), 2);
}

void SettledTests::test_combine_set_all_settled_waits_for_canceled_input()
{
    auto pending = deferred<void>();

    auto failFast = combine({canceledVoid(), pending.future()});
    auto allSettled = combine({canceledVoid(), pending.future()}, AllSettled);

    // The two modes differ in when the cancel lands, not whether it does.
    QVERIFY(failFast.future().isCanceled());
    QVERIFY(!allSettled.future().isFinished());

    pending.complete();
    QVERIFY(waitUntil([&]() {
        return allSettled.future().isFinished();
    }, 1000));
    QVERIFY(allSettled.future().isCanceled());
}

void SettledTests::test_combine_set_off_thread()
{
    auto pending = deferred<void>();
    auto settled = settledVoid();

    // Off the main thread the inline path is skipped, so the input watchers
    // settle on the main thread while seal() runs on the worker. The join must
    // still settle, and settle correctly.
    auto worker = QtConcurrent::run([settled, pending]() -> QFuture<void> {
        return combine({settled, pending.future()}).future();
    });
    QVERIFY(waitUntil(worker, 1000));

    auto joined = worker.result();
    pending.complete();

    QVERIFY(waitUntil(joined, 1000));
    QVERIFY(joined.isFinished());
    QVERIFY(!joined.isCanceled());
}

void SettledTests::test_combine_set_nested_is_immediate()
{
    auto leaf = combine({settledVoid(), settledVoid()});

    auto node = deferred<void>();
    node.complete(leaf.future());

    auto root = combine({node.future(), settledVoid()});

    // A two level dependency graph of finished work settles end to end without
    // a single trip through the event loop.
    QVERIFY(root.future().isFinished());
    QVERIFY(!root.future().isCanceled());
}
