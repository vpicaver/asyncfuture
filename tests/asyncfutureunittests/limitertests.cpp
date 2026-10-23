#include <QTest>
#include <QThread>
#include <memory>
#include <stdexcept>
#include <asyncfuture.h>
#include "testfunctions.h"
#include "limitertests.h"
#include "limittestfixtures.h"

using namespace AsyncFuture;
using namespace Test;
using namespace LimitTestFixtures;

namespace {

constexpr qint64 kLedgerCap = 100;

// A launcher that throws when it is called.
QFuture<int> launchThrowing()
{
    throw std::runtime_error("launch failed");
}

// The nullary launch Limiter::admit() takes, handing input to launcher.
template <typename AnyLauncher>
auto launchOf(AnyLauncher& launcher, int input)
{
    return [launch = launcher.launch(), input]() { return launch(input); };
}

// A thread with an event loop and a context object living on it. Deleting the
// context on its own thread, then quitting and waiting, ends it before any
// ledger declared ahead of it is destroyed.
struct WorkerThread {
    QThread thread;
    QObject* context = new QObject();

    WorkerThread()
    {
        context->moveToThread(&thread);
        thread.start();
    }

    ~WorkerThread()
    {
        QMetaObject::invokeMethod(context, [doomed = context]() { delete doomed; },
                                  Qt::BlockingQueuedConnection);
        thread.quit();
        thread.wait();
    }
};

} // namespace

LimiterTests::LimiterTests(QObject *parent) : QObject(parent)
{
    // This function do nothing but could make Qt Creator Autotests plugin recognize this test
    auto ref =[this]() {
        QTest::qExec(this, 0, 0);
    };
    Q_UNUSED(ref);
}

void LimiterTests::test_item_waits_for_running_item()
{
    Launcher launcher;
    Limiter limiter(Limit{kOneAtATime});

    QFuture<int> first = limiter.admit(launchOf(launcher, 0));
    QFuture<int> second = limiter.admit(launchOf(launcher, 1));
    static_assert(std::is_same_v<decltype(limiter.admit(launchOf(launcher, 0))), QFuture<int>>);
    tick();

    QCOMPARE(launcher.order, QList<int>({0}));
    QVERIFY(!isSettled(second));

    launcher.complete(0);
    QVERIFY(waitUntil([&]() { return launcher.order.size() == 2; }, kTimeoutMs));
    QCOMPARE(launcher.order, QList<int>({0, 1}));
    QVERIFY(waitUntil(first, kTimeoutMs));
    QCOMPARE(first.result(), kResultOffset);

    launcher.complete(1);
    QVERIFY(waitUntil(second, kTimeoutMs));
    QCOMPARE(second.result(), 1 + kResultOffset);
}

void LimiterTests::test_max_running_holds_as_items_arrive()
{
    constexpr int maxRunning = 2;
    constexpr int arrivingCount = 4;
    Launcher launcher;
    Limiter limiter(Limit{maxRunning});

    QList<QFuture<int>> items;
    for (int input = 0; input < arrivingCount; ++input) {
        items.append(limiter.admit(launchOf(launcher, input)));
        tick();
        QVERIFY(launcher.running() <= maxRunning);
    }
    QCOMPARE(launcher.order, QList<int>({0, 1}));

    for (int input = 0; input < arrivingCount; ++input) {
        QVERIFY(waitUntil([&]() { return launcher.order.contains(input); }, kTimeoutMs));
        QVERIFY(launcher.running() <= maxRunning);
        launcher.complete(input);
    }
    for (const QFuture<int>& item : std::as_const(items)) {
        QVERIFY(waitUntil(item, kTimeoutMs));
    }

    QCOMPARE(launcher.peakRunning, maxRunning);
    QCOMPARE(launcher.order, QList<int>({0, 1, 2, 3}));
}

void LimiterTests::test_weight_budget_holds_until_enough_weight_returns()
{
    constexpr qint64 budget = 7;
    constexpr qint64 lightWeight = 3;
    constexpr qint64 heavyWeight = 5;

    Launcher launcher;
    launcher.weights = {lightWeight, lightWeight, heavyWeight};
    Limiter limiter(Limit{kManyRunning, budget});

    QList<QFuture<int>> items;
    for (int input = 0; input < launcher.weights.size(); ++input) {
        items.append(limiter.admit(launchOf(launcher, input), launcher.weightOf(input)));
    }
    tick();
    QCOMPARE(launcher.order, QList<int>({0, 1}));

    // One light item back leaves 3 + 5 over the budget.
    launcher.complete(0);
    tick();
    QCOMPARE(launcher.order, QList<int>({0, 1}));

    launcher.complete(1);
    QVERIFY(waitUntil([&]() { return launcher.order.size() == 3; }, kTimeoutMs));
    QCOMPARE(launcher.order, QList<int>({0, 1, 2}));

    launcher.complete(2);
    QVERIFY(waitUntil(items.at(2), kTimeoutMs));
    QVERIFY(launcher.peakWeight <= budget);
}

void LimiterTests::test_first_item_runs_past_budget()
{
    constexpr int maxRunning = 2;
    constexpr qint64 budget = 5;
    constexpr qint64 heavyWeight = 10;
    constexpr qint64 lightWeight = 1;

    Launcher launcher;
    launcher.weights = {heavyWeight, lightWeight};
    Limiter limiter(Limit{maxRunning, budget});

    QFuture<int> heavy = limiter.admit(launchOf(launcher, 0), heavyWeight);
    QFuture<int> light = limiter.admit(launchOf(launcher, 1), lightWeight);
    tick();

    // The heavy item runs alone, and the light one waits for it.
    QCOMPARE(launcher.order, QList<int>({0}));

    launcher.complete(0);
    QVERIFY(waitUntil([&]() { return launcher.order.size() == 2; }, kTimeoutMs));
    QCOMPARE(heavy.result(), kResultOffset);

    launcher.complete(1);
    QVERIFY(waitUntil(light, kTimeoutMs));
    QCOMPARE(light.result(), 1 + kResultOffset);
}

void LimiterTests::test_ledger_charged_on_admission_released_on_finish()
{
    constexpr qint64 ledgerCap = 5;
    constexpr qint64 heavyWeight = 10;
    constexpr qint64 lightWeight = 1;

    ByteLedger ledger(ledgerCap);
    DrainEventsOnExit drainBeforeLedgerDies;
    Launcher launcher;
    Limiter limiter(Limit{kManyRunning, kCountOnly, &ledger});

    QFuture<int> heavy = limiter.admit(launchOf(launcher, 0), heavyWeight);
    QFuture<int> light = limiter.admit(launchOf(launcher, 1), lightWeight);
    tick();

    // The first item of the window is charged past the cap; the ledger holds the light one back.
    QCOMPARE(launcher.order, QList<int>({0}));
    QCOMPARE(ledger.charged(), heavyWeight);

    launcher.complete(0);
    QVERIFY(waitUntil([&]() { return launcher.order.size() == 2; }, kTimeoutMs));
    QCOMPARE(ledger.charged(), lightWeight);

    launcher.complete(1);
    QVERIFY(waitUntil(light, kTimeoutMs));
    QVERIFY(waitUntil([&]() { return ledger.charged() == 0; }, kTimeoutMs));
    QCOMPARE(heavy.result(), kResultOffset);
}

void LimiterTests::test_release_from_another_limiter_wakes_admission()
{
    constexpr int itemCount = 2;
    constexpr qint64 ledgerCap = 3;

    ByteLedger ledger(ledgerCap);
    DrainEventsOnExit drainBeforeLedgerDies;
    Launcher launcherA;
    Launcher launcherB;
    Limiter limiterA(Limit{kManyRunning, kCountOnly, &ledger});
    Limiter limiterB(Limit{kManyRunning, kCountOnly, &ledger});

    QList<QFuture<int>> itemsA;
    QList<QFuture<int>> itemsB;
    for (int input = 0; input < itemCount; ++input) {
        itemsA.append(limiterA.admit(launchOf(launcherA, input)));
    }
    for (int input = 0; input < itemCount; ++input) {
        itemsB.append(limiterB.admit(launchOf(launcherB, input)));
    }
    tick();

    QCOMPARE(launcherA.order, QList<int>({0, 1}));
    QCOMPARE(launcherB.order, QList<int>({0}));
    QCOMPARE(ledger.charged(), ledgerCap);

    // A's release wakes B, though none of B's items finished.
    launcherA.complete(0);
    QVERIFY(waitUntil([&]() { return launcherB.order.size() == itemCount; }, kTimeoutMs));
    QVERIFY(!isSettled(launcherB.launched.at(0).future()));
    QCOMPARE(ledger.charged(), ledgerCap);

    launcherA.complete(1);
    launcherB.complete(0);
    launcherB.complete(1);
    QVERIFY(waitUntil(itemsB.at(1), kTimeoutMs));
    QVERIFY(waitUntil([&]() { return ledger.charged() == 0; }, kTimeoutMs));
}

void LimiterTests::test_cancel_queued_item_skips_it()
{
    constexpr int itemCount = 3;
    constexpr int skipped = 1;
    Launcher launcher;
    Limiter limiter(Limit{kOneAtATime});

    QList<QFuture<int>> items;
    for (int input = 0; input < itemCount; ++input) {
        items.append(limiter.admit(launchOf(launcher, input)));
    }
    tick();
    QCOMPARE(launcher.order, QList<int>({0}));

    QFuture<int> skippedItem = items.at(skipped);
    skippedItem.cancel();
    tick();

    launcher.complete(0);
    QVERIFY(waitUntil([&]() { return launcher.order.size() == 2; }, kTimeoutMs));
    QCOMPARE(launcher.order, QList<int>({0, 2}));
    QVERIFY(skippedItem.isCanceled());

    launcher.complete(2);
    QVERIFY(waitUntil(items.at(2), kTimeoutMs));
    QCOMPARE(items.at(2).result(), 2 + kResultOffset);
    QVERIFY(waitUntil([&]() { return limiter.runningCount() == 0; }, kTimeoutMs));
    QCOMPARE(limiter.queuedCount(), 0);
    QCOMPARE(launcher.order, QList<int>({0, 2}));
}

void LimiterTests::test_cancel_admitted_item_propagates_and_holds_slot()
{
    PromiseLauncher launcher;
    Limiter limiter(Limit{kOneAtATime});

    QFuture<int> first = limiter.admit(launchOf(launcher, 0));
    QFuture<int> second = limiter.admit(launchOf(launcher, 1));
    tick();
    QCOMPARE(launcher.order, QList<int>({0}));

    first.cancel();
    tick();
    QVERIFY(launcher.launched(0).isCanceled());
    QVERIFY(!launcher.launched(0).isFinished());
    QCOMPARE(launcher.order, QList<int>({0}));

    // The slot frees once the canceled worker exits.
    launcher.finish(0);
    QVERIFY(waitUntil([&]() { return launcher.order.size() == 2; }, kTimeoutMs));
    QCOMPARE(launcher.order, QList<int>({0, 1}));

    launcher.complete(1);
    QVERIFY(waitUntil(second, kTimeoutMs));
    QCOMPARE(second.result(), 1 + kResultOffset);
}

void LimiterTests::test_destroying_limiter_cancels_queued_and_running_settles()
{
    constexpr qint64 runningWeight = 3;
    constexpr qint64 queuedWeight = 5;

    ByteLedger ledger(kLedgerCap);
    DrainEventsOnExit drainBeforeLedgerDies;
    PromiseLauncher launcher;
    auto limiter = std::make_unique<Limiter>(Limit{kOneAtATime, kCountOnly, &ledger});

    QFuture<int> running = limiter->admit(launchOf(launcher, 0), runningWeight);
    QFuture<int> queued = limiter->admit(launchOf(launcher, 1), queuedWeight);
    tick();
    QCOMPARE(launcher.order, QList<int>({0}));
    QCOMPARE(ledger.charged(), runningWeight);

    limiter.reset();
    tick();

    QVERIFY(queued.isCanceled());
    QVERIFY(!running.isCanceled());
    QVERIFY(!launcher.launched(0).isCanceled());
    QCOMPARE(ledger.charged(), runningWeight);

    launcher.complete(0);
    QVERIFY(waitUntil([&]() { return ledger.charged() == 0; }, kTimeoutMs));
    QVERIFY(waitUntil(running, kTimeoutMs));
    QCOMPARE(running.result(), kResultOffset);
    QCOMPARE(launcher.order, QList<int>({0}));
}

void LimiterTests::test_counts_follow_every_transition()
{
    PromiseLauncher launcher;
    Limiter limiter(Limit{kOneAtATime});
    QCOMPARE(limiter.queuedCount(), 0);
    QCOMPARE(limiter.runningCount(), 0);
    QCOMPARE(limiter.limit().maxRunning, kOneAtATime);

    QFuture<int> first = limiter.admit(launchOf(launcher, 0));
    QCOMPARE(limiter.queuedCount(), 0);
    QCOMPARE(limiter.runningCount(), 1);

    limiter.admit(launchOf(launcher, 1));
    QCOMPARE(limiter.queuedCount(), 1);
    QCOMPARE(limiter.runningCount(), 1);

    limiter.admit(launchOf(launcher, 2));
    QCOMPARE(limiter.queuedCount(), 2);
    QCOMPARE(limiter.runningCount(), 1);

    // A canceled item keeps running until its worker exits.
    first.cancel();
    tick();
    QCOMPARE(limiter.queuedCount(), 2);
    QCOMPARE(limiter.runningCount(), 1);

    launcher.finish(0);
    QVERIFY(waitUntil([&]() { return launcher.order.size() == 2; }, kTimeoutMs));
    QCOMPARE(limiter.queuedCount(), 1);
    QCOMPARE(limiter.runningCount(), 1);

    launcher.complete(1);
    QVERIFY(waitUntil([&]() { return launcher.order.size() == 3; }, kTimeoutMs));
    QCOMPARE(limiter.queuedCount(), 0);
    QCOMPARE(limiter.runningCount(), 1);

    launcher.complete(2);
    QVERIFY(waitUntil([&]() { return limiter.runningCount() == 0; }, kTimeoutMs));
    QCOMPARE(limiter.queuedCount(), 0);
}

void LimiterTests::test_admit_from_inside_launcher_queues_behind_running_item()
{
    Launcher launcher;
    Limiter limiter(Limit{kOneAtATime});

    QFuture<int> inner;
    auto launchAndAdmit = [&limiter, &inner, &launcher, launchOuter = launchOf(launcher, 0)]() {
        QFuture<int> outer = launchOuter();
        inner = limiter.admit(launchOf(launcher, 1));
        return outer;
    };

    QFuture<int> outer = limiter.admit(launchAndAdmit);
    tick();
    QCOMPARE(launcher.order, QList<int>({0}));
    QCOMPARE(limiter.queuedCount(), 1);
    QCOMPARE(limiter.runningCount(), 1);
    QVERIFY(!isSettled(inner));

    launcher.complete(0);
    QVERIFY(waitUntil([&]() { return launcher.order.size() == 2; }, kTimeoutMs));
    QCOMPARE(launcher.order, QList<int>({0, 1}));
    QCOMPARE(outer.result(), kResultOffset);

    launcher.complete(1);
    QVERIFY(waitUntil(inner, kTimeoutMs));
    QCOMPARE(inner.result(), 1 + kResultOffset);
}

void LimiterTests::test_throwing_launcher_cancels_item_with_ledger_and_counts_unchanged()
{
    constexpr qint64 throwingWeight = 3;
    constexpr qint64 itemWeight = 5;

    ByteLedger ledger(kLedgerCap);
    DrainEventsOnExit drainBeforeLedgerDies;
    Launcher launcher;
    Limiter limiter(Limit{kOneAtATime, kCountOnly, &ledger});

    QFuture<int> throwing;
    bool isThrown = false;
    try {
        throwing = limiter.admit(launchThrowing, throwingWeight);
    } catch (...) {
        isThrown = true;
    }
    QVERIFY(!isThrown);
    QVERIFY(throwing.isCanceled());
    QCOMPARE(ledger.charged(), qint64(0));
    QCOMPARE(limiter.queuedCount(), 0);
    QCOMPARE(limiter.runningCount(), 0);

    // The thrown item holds no slot and no charge, and is never retried.
    QFuture<int> first = limiter.admit(launchOf(launcher, 0), itemWeight);
    QFuture<int> second = limiter.admit(launchOf(launcher, 1), itemWeight);
    tick();
    QCOMPARE(launcher.order, QList<int>({0}));
    QCOMPARE(ledger.charged(), itemWeight);
    QCOMPARE(limiter.queuedCount(), 1);
    QCOMPARE(limiter.runningCount(), 1);

    launcher.complete(0);
    QVERIFY(waitUntil([&]() { return launcher.order.size() == 2; }, kTimeoutMs));
    launcher.complete(1);
    QVERIFY(waitUntil(second, kTimeoutMs));
    QVERIFY(waitUntil([&]() { return ledger.charged() == 0; }, kTimeoutMs));
    QCOMPARE(first.result(), kResultOffset);
}

void LimiterTests::test_launcher_throwing_inside_another_callers_admit_cancels_only_its_item()
{
    constexpr qint64 startingCap = 5;
    constexpr qint64 raisedCap = 100;
    constexpr qint64 runningWeight = 1;
    constexpr qint64 oversizeWeight = 10;
    constexpr qint64 plainWeight = 2;

    FakeLedger ledger(startingCap);
    DrainEventsOnExit drainBeforeLedgerDies;
    Launcher launcher;
    Limiter limiter(Limit{kManyRunning, kCountOnly, &ledger});

    QFuture<int> running = limiter.admit(launchOf(launcher, 0), runningWeight);
    bool isOversizeLaunched = false;
    auto launchOversize = [&isOversizeLaunched]() {
        isOversizeLaunched = true;
        return launchThrowing();
    };
    QFuture<int> oversize = limiter.admit(launchOversize, oversizeWeight);
    QCOMPARE(limiter.queuedCount(), 1);
    QCOMPARE(ledger.refusedCount, 1);

    // The raised cap lets the oversize item through on the next caller's admit().
    ledger.cap = raisedCap;
    QFuture<int> plain;
    bool isThrown = false;
    try {
        plain = limiter.admit(launchOf(launcher, 1), plainWeight);
    } catch (...) {
        isThrown = true;
    }
    QVERIFY(!isThrown);
    QVERIFY(isOversizeLaunched);
    QVERIFY(oversize.isCanceled());
    QCOMPARE(launcher.order, QList<int>({0, 1}));
    QVERIFY(!plain.isCanceled());
    QCOMPARE(ledger.charged, runningWeight + plainWeight);
    QCOMPARE(limiter.queuedCount(), 0);
    QCOMPARE(limiter.runningCount(), 2);

    launcher.complete(0);
    launcher.complete(1);
    QVERIFY(waitUntil(plain, kTimeoutMs));
    QCOMPARE(plain.result(), 1 + kResultOffset);
    QCOMPARE(running.result(), kResultOffset);
    QVERIFY(waitUntil([&]() { return ledger.charged == 0; }, kTimeoutMs));
}

void LimiterTests::test_cancel_queued_cancels_waiting_items_and_keeps_running_one()
{
    constexpr int itemCount = 3;
    constexpr int laterInput = itemCount;
    Launcher launcher;
    Limiter limiter(Limit{kOneAtATime});

    QList<QFuture<int>> items;
    for (int input = 0; input < itemCount; ++input) {
        items.append(limiter.admit(launchOf(launcher, input)));
    }
    QCOMPARE(limiter.queuedCount(), itemCount - 1);

    limiter.cancelQueued();
    QCOMPARE(limiter.queuedCount(), 0);
    QCOMPARE(limiter.runningCount(), 1);
    QVERIFY(!items.at(0).isCanceled());
    for (int input = 1; input < itemCount; ++input) {
        QVERIFY(items.at(input).isCanceled());
    }

    launcher.complete(0);
    QVERIFY(waitUntil(items.at(0), kTimeoutMs));
    QCOMPARE(items.at(0).result(), kResultOffset);
    QVERIFY(waitUntil([&]() { return limiter.runningCount() == 0; }, kTimeoutMs));
    QCOMPARE(launcher.order, QList<int>({0}));

    // The Limiter keeps admitting after the queue is canceled.
    QFuture<int> later = limiter.admit(launchOf(launcher, laterInput));
    QCOMPARE(launcher.order, QList<int>({0, laterInput}));
    launcher.complete(laterInput);
    QVERIFY(waitUntil(later, kTimeoutMs));
    QCOMPARE(later.result(), laterInput + kResultOffset);
}

void LimiterTests::test_queued_count_drops_canceled_item_before_it_reaches_front()
{
    constexpr int itemCount = 3;
    constexpr int canceledInput = 2;
    Launcher launcher;
    Limiter limiter(Limit{kOneAtATime});

    QList<QFuture<int>> items;
    for (int input = 0; input < itemCount; ++input) {
        items.append(limiter.admit(launchOf(launcher, input)));
    }
    QCOMPARE(limiter.queuedCount(), itemCount - 1);

    QFuture<int> canceled = items.at(canceledInput);
    canceled.cancel();
    QCOMPARE(limiter.queuedCount(), itemCount - 2);

    launcher.complete(0);
    QVERIFY(waitUntil([&]() { return launcher.order.size() == 2; }, kTimeoutMs));
    QCOMPARE(limiter.queuedCount(), 0);

    launcher.complete(1);
    QVERIFY(waitUntil([&]() { return limiter.runningCount() == 0; }, kTimeoutMs));
    QCOMPARE(launcher.order, QList<int>({0, 1}));
    QCOMPARE(limiter.queuedCount(), 0);
}

void LimiterTests::test_release_on_worker_thread_wakes_main_thread_limiter()
{
    constexpr qint64 ledgerCap = 5;
    constexpr qint64 workerWeight = ledgerCap;
    constexpr qint64 mainWeight = 1;

    ByteLedger ledger(ledgerCap);
    DrainEventsOnExit drainBeforeLedgerDies;
    WorkerThread worker;

    Deferred<int> workerDefer;
    QFuture<int> workerItem;
    QMetaObject::invokeMethod(worker.context, [&]() {
        auto* workerLimiter = new Limiter(Limit{kManyRunning, kCountOnly, &ledger}, worker.context);
        workerItem = workerLimiter->admit([workerDefer]() { return workerDefer.future(); }, workerWeight);
    }, Qt::BlockingQueuedConnection);
    QCOMPARE(ledger.charged(), workerWeight);

    // The main Limiter's first item is charged past the cap; the second is refused.
    Launcher launcher;
    Limiter mainLimiter(Limit{kManyRunning, kCountOnly, &ledger});
    QFuture<int> mainFirst = mainLimiter.admit(launchOf(launcher, 0), mainWeight);
    QFuture<int> mainSecond = mainLimiter.admit(launchOf(launcher, 1), mainWeight);
    QCOMPARE(launcher.order, QList<int>({0}));
    QCOMPARE(mainLimiter.queuedCount(), 1);

    // The release runs on the worker thread and reaches the main Limiter queued.
    QMetaObject::invokeMethod(worker.context, [&]() {
        Deferred<int> defer = workerDefer;
        defer.complete(kResultOffset);
    }, Qt::BlockingQueuedConnection);
    QVERIFY(waitUntil([&]() { return launcher.order.size() == 2; }, kTimeoutMs));
    QCOMPARE(launcher.order, QList<int>({0, 1}));
    QVERIFY(waitUntil(workerItem, kTimeoutMs));
    QCOMPARE(workerItem.result(), kResultOffset);

    launcher.complete(0);
    launcher.complete(1);
    QVERIFY(waitUntil(mainSecond, kTimeoutMs));
    QCOMPARE(mainFirst.result(), kResultOffset);
    QVERIFY(waitUntil([&]() { return ledger.charged() == 0; }, kTimeoutMs));
}

void LimiterTests::test_fifo_holds_behind_item_that_does_not_fit()
{
    constexpr qint64 budget = 7;
    constexpr qint64 heavyWeight = 5;
    constexpr qint64 lightWeight = 1;

    Launcher launcher;
    launcher.weights = {heavyWeight, heavyWeight, lightWeight};
    Limiter limiter(Limit{kManyRunning, budget});

    QList<QFuture<int>> items;
    for (int input = 0; input < launcher.weights.size(); ++input) {
        items.append(limiter.admit(launchOf(launcher, input), launcher.weightOf(input)));
    }
    tick();

    // The light item fits beside the running heavy one, yet waits behind the
    // heavy item queued ahead of it.
    QCOMPARE(launcher.order, QList<int>({0}));

    // Once the heavy item ahead runs, the light one fits beside it.
    launcher.complete(0);
    QVERIFY(waitUntil([&]() { return launcher.order.size() == 3; }, kTimeoutMs));
    QCOMPARE(launcher.order, QList<int>({0, 1, 2}));

    launcher.complete(1);
    launcher.complete(2);
    QVERIFY(waitUntil(items.at(2), kTimeoutMs));
}

void LimiterTests::test_launcher_throwing_after_release_cancels_item_and_queue_moves_on()
{
    constexpr qint64 itemWeight = 3;

    ByteLedger ledger(kLedgerCap);
    DrainEventsOnExit drainBeforeLedgerDies;
    Launcher launcher;
    Limiter limiter(Limit{kOneAtATime, kCountOnly, &ledger});

    QFuture<int> first = limiter.admit(launchOf(launcher, 0), itemWeight);
    QFuture<int> throwing = limiter.admit(launchThrowing, itemWeight);
    QFuture<int> last = limiter.admit(launchOf(launcher, 2), itemWeight);
    tick();
    QCOMPARE(launcher.order, QList<int>({0}));

    // The throwing launcher runs from the first item's release, where no caller
    // can receive the exception.
    launcher.complete(0);
    QVERIFY(waitUntil([&]() { return launcher.order.size() == 2; }, kTimeoutMs));
    QCOMPARE(launcher.order, QList<int>({0, 2}));
    QVERIFY(throwing.isCanceled());
    QCOMPARE(ledger.charged(), itemWeight);
    QCOMPARE(limiter.queuedCount(), 0);
    QCOMPARE(limiter.runningCount(), 1);

    launcher.complete(2);
    QVERIFY(waitUntil(last, kTimeoutMs));
    QCOMPARE(last.result(), 2 + kResultOffset);
    QVERIFY(waitUntil([&]() { return ledger.charged() == 0; }, kTimeoutMs));
    QCOMPARE(first.result(), kResultOffset);
}

void LimiterTests::test_limited_launcher_throwing_after_release_cancels_item_and_run_completes()
{
    constexpr int itemCount = 3;
    constexpr int throwingInput = 1;

    QObject context;
    Launcher launcher;
    auto launchOrThrow = [launch = launcher.launch()](int input) {
        if (input == throwingInput) {
            return launchThrowing();
        }
        return launch(input);
    };

    QList<int> inputs;
    for (int input = 0; input < itemCount; ++input) {
        inputs.append(input);
    }
    auto run = limited(&context, inputs, launchOrThrow, Limit{kOneAtATime});
    tick();
    QCOMPARE(launcher.order, QList<int>({0}));

    launcher.complete(0);
    QVERIFY(waitUntil([&]() { return launcher.order.size() == 2; }, kTimeoutMs));
    QCOMPARE(launcher.order, QList<int>({0, 2}));
    QVERIFY(run.items.at(throwingInput).isCanceled());

    launcher.complete(2);
    QVERIFY(waitUntil(run.all, kTimeoutMs));
    QVERIFY(!run.all.isCanceled());
    QCOMPARE(run.items.at(2).result(), 2 + kResultOffset);
}
