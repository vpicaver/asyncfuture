#include <QTest>
#include <numeric>
#include <Automator>
#include <asyncfuture.h>
#include "testfunctions.h"
#include "limitedtests.h"

using namespace AsyncFuture;
using namespace Test;

namespace {

constexpr int kTimeoutMs = 1000;
constexpr int kItemCount = 5;
constexpr int kResultOffset = 100;
constexpr int kOneAtATime = 1;
constexpr qint64 kCountOnly = 0;
constexpr qint64 kDefaultWeight = 1;
constexpr int kManyRunning = 10;

template <typename T>
bool isSettled(const QFuture<T>& future)
{
    return future.isFinished() || future.isCanceled();
}

// Launches one Deferred per admitted input, so the test decides when each
// item settles.
class Launcher {
public:
    QList<int> order;
    QList<Deferred<int>> launched;
    int peakRunning = 0;
    qint64 peakWeight = 0;
    QList<qint64> weights;

    int running() const
    {
        int count = 0;
        for (const Deferred<int>& defer : launched) {
            if (!isSettled(defer.future())) {
                ++count;
            }
        }
        return count;
    }

    qint64 runningWeight() const
    {
        qint64 total = 0;
        for (int i = 0; i < launched.size(); ++i) {
            if (!isSettled(launched.at(i).future())) {
                total += weightOf(order.at(i));
            }
        }
        return total;
    }

    qint64 weightOf(int input) const
    {
        return weights.isEmpty() ? kDefaultWeight : weights.at(input);
    }

    std::function<QFuture<int>(int)> launch()
    {
        return [this](int input) {
            Deferred<int> defer;
            order.append(input);
            launched.append(defer);
            peakRunning = std::max(peakRunning, running());
            peakWeight = std::max(peakWeight, runningWeight());
            return defer.future();
        };
    }

    std::function<qint64(int)> weight() const
    {
        return [this](int input) { return weightOf(input); };
    }

    // Settles the item that was launched for input.
    void complete(int input)
    {
        Deferred<int> defer = launched.at(order.indexOf(input));
        defer.complete(input + kResultOffset);
        tick();
    }
};

// Records every charge and release against a fixed cap.
class FakeLedger : public Ledger {
public:
    explicit FakeLedger(qint64 cap) : cap(cap) {}

    bool tryCharge(qint64 bytes) override
    {
        if (charged + bytes > cap) {
            ++refusedCount;
            return false;
        }
        charged += bytes;
        ++chargeCount;
        return true;
    }

    void charge(qint64 bytes) override
    {
        charged += bytes;
        ++chargeCount;
        ++forcedChargeCount;
    }

    void release(qint64 bytes) override
    {
        charged -= bytes;
        ++releaseCount;
        emit released();
    }

    qint64 cap = 0;
    qint64 charged = 0;
    int chargeCount = 0;
    int forcedChargeCount = 0;
    int releaseCount = 0;
    int refusedCount = 0;
};

// Declared right after a ledger, so a test that fails with items still
// running delivers their releases before the ledger is destroyed.
struct DrainEventsOnExit {
    ~DrainEventsOnExit() { tick(); }
};

QList<int> inputRange(int count)
{
    QList<int> inputs;
    for (int i = 0; i < count; ++i) {
        inputs.append(i);
    }
    return inputs;
}

} // namespace

LimitedTests::LimitedTests(QObject *parent) : QObject(parent)
{
    // This function do nothing but could make Qt Creator Autotests plugin recognize this test
    auto ref =[this]() {
        QTest::qExec(this, 0, 0);
    };
    Q_UNUSED(ref);
}

void LimitedTests::test_count_limit_holds()
{
    constexpr int maxRunning = 2;
    Launcher launcher;
    QObject context;

    auto limitedRun = limited(&context, inputRange(kItemCount),
                              launcher.launch(), launcher.weight(),
                              Limit{maxRunning, kCountOnly});
    tick();

    QCOMPARE(launcher.order.size(), maxRunning);

    for (int input = 0; input < kItemCount; ++input) {
        QVERIFY(waitUntil([&]() { return launcher.order.contains(input); }, kTimeoutMs));
        QVERIFY(launcher.running() <= maxRunning);
        launcher.complete(input);
    }

    QVERIFY(waitUntil(limitedRun.all, kTimeoutMs));
    QCOMPARE(launcher.peakRunning, maxRunning);
    QCOMPARE(launcher.order.size(), kItemCount);
}

void LimitedTests::test_weight_limit_holds()
{
    constexpr int maxRunning = 10;
    constexpr qint64 itemWeight = 3;
    constexpr qint64 budget = 7;
    constexpr int fitsInBudget = 2;

    Launcher launcher;
    launcher.weights = QList<qint64>(kItemCount, itemWeight);
    QObject context;

    auto limitedRun = limited(&context, inputRange(kItemCount),
                              launcher.launch(), launcher.weight(),
                              Limit{maxRunning, budget});
    tick();

    QCOMPARE(launcher.order.size(), fitsInBudget);

    for (int input = 0; input < kItemCount; ++input) {
        QVERIFY(waitUntil([&]() { return launcher.order.contains(input); }, kTimeoutMs));
        QVERIFY(launcher.runningWeight() <= budget);
        launcher.complete(input);
    }

    QVERIFY(waitUntil(limitedRun.all, kTimeoutMs));
    QCOMPARE(launcher.peakRunning, fitsInBudget);
    QVERIFY(launcher.peakWeight <= budget);
}

void LimitedTests::test_first_item_runs_past_budget()
{
    constexpr int maxRunning = 2;
    constexpr qint64 budget = 5;
    constexpr qint64 heavyWeight = 10;
    constexpr qint64 lightWeight = 1;

    Launcher launcher;
    launcher.weights = {heavyWeight, lightWeight};
    QObject context;

    auto limitedRun = limited(&context, inputRange(launcher.weights.size()),
                              launcher.launch(), launcher.weight(),
                              Limit{maxRunning, budget});
    tick();

    // The heavy item runs alone, and the light one waits for it.
    QCOMPARE(launcher.order, QList<int>({0}));

    launcher.complete(0);
    QVERIFY(waitUntil([&]() { return launcher.order.size() == 2; }, kTimeoutMs));
    QCOMPARE(launcher.order, QList<int>({0, 1}));

    launcher.complete(1);
    QVERIFY(waitUntil(limitedRun.all, kTimeoutMs));
    QCOMPARE(limitedRun.items.at(0).result(), kResultOffset);
}

void LimitedTests::test_fifo_order()
{
    constexpr int maxRunning = 4;
    constexpr qint64 budget = 7;
    constexpr qint64 heavyWeight = 5;
    constexpr qint64 lightWeight = 1;

    Launcher launcher;
    launcher.weights = {heavyWeight, lightWeight, heavyWeight, lightWeight};
    QObject context;

    auto limitedRun = limited(&context, inputRange(launcher.weights.size()),
                              launcher.launch(), launcher.weight(),
                              Limit{maxRunning, budget});
    tick();

    // Item 3 would fit beside 0 and 1, but it waits behind item 2.
    QCOMPARE(launcher.order, QList<int>({0, 1}));

    launcher.complete(1);
    QCOMPARE(launcher.order, QList<int>({0, 1}));

    launcher.complete(0);
    QVERIFY(waitUntil([&]() { return launcher.order.size() == 4; }, kTimeoutMs));
    QCOMPARE(launcher.order, QList<int>({0, 1, 2, 3}));

    launcher.complete(3);
    launcher.complete(2);
    QVERIFY(waitUntil(limitedRun.all, kTimeoutMs));
    for (int input = 0; input < limitedRun.items.size(); ++input) {
        QCOMPARE(limitedRun.items.at(input).result(), input + kResultOffset);
    }
}

void LimitedTests::test_launch_count_equals_admitted_count()
{
    constexpr int maxRunning = 2;
    Launcher launcher;
    QObject context;

    auto limitedRun = limited(&context, inputRange(kItemCount),
                              launcher.launch(), launcher.weight(),
                              Limit{maxRunning, kCountOnly});
    tick();

    for (int settled = 0; settled < kItemCount; ++settled) {
        const int admitted = std::min(settled + maxRunning, kItemCount);
        QVERIFY(waitUntil([&]() { return launcher.order.size() == admitted; }, kTimeoutMs));
        tick();
        QCOMPARE(launcher.order.size(), admitted);
        QCOMPARE(launcher.order, inputRange(admitted));
        launcher.complete(settled);
    }

    QVERIFY(waitUntil(limitedRun.all, kTimeoutMs));
    QCOMPARE(launcher.order, inputRange(kItemCount));
}

void LimitedTests::test_cancel_all_before_admission()
{
    Launcher launcher;
    QObject context;

    auto limitedRun = limited(&context, inputRange(kItemCount),
                              launcher.launch(), launcher.weight(),
                              Limit{kOneAtATime, kCountOnly});
    tick();
    QCOMPARE(launcher.order.size(), kOneAtATime);

    limitedRun.all.cancel();
    tick();

    QVERIFY(waitUntil([&]() { return launcher.launched.at(0).future().isCanceled(); }, kTimeoutMs));
    for (const QFuture<int>& item : limitedRun.items) {
        QVERIFY(waitUntil([&]() { return item.isCanceled(); }, kTimeoutMs));
    }
    tick();
    QCOMPARE(launcher.order.size(), kOneAtATime);
}

void LimitedTests::test_cancel_one_before_admission()
{
    constexpr int skipped = 1;
    constexpr int itemCount = 3;
    Launcher launcher;
    QObject context;

    auto limitedRun = limited(&context, inputRange(itemCount),
                              launcher.launch(), launcher.weight(),
                              Limit{kOneAtATime, kCountOnly});
    tick();
    QCOMPARE(launcher.order, QList<int>({0}));

    QFuture<int> skippedItem = limitedRun.items.at(skipped);
    skippedItem.cancel();
    tick();

    launcher.complete(0);
    QVERIFY(waitUntil([&]() { return launcher.order.size() == 2; }, kTimeoutMs));
    QCOMPARE(launcher.order, QList<int>({0, 2}));

    launcher.complete(2);
    QVERIFY(waitUntil(limitedRun.all, kTimeoutMs));
    QVERIFY(skippedItem.isCanceled());
    QCOMPARE(limitedRun.items.at(2).result(), 2 + kResultOffset);
    QCOMPARE(launcher.order, QList<int>({0, 2}));
}

void LimitedTests::test_context_destroyed_mid_run()
{
    Launcher launcher;
    auto context = std::make_unique<QObject>();

    auto limitedRun = limited(context.get(), inputRange(kItemCount),
                              launcher.launch(), launcher.weight(),
                              Limit{kOneAtATime, kCountOnly});
    tick();
    QCOMPARE(launcher.order.size(), kOneAtATime);

    context.reset();
    tick();

    launcher.complete(0);
    tick();

    for (int input = 1; input < kItemCount; ++input) {
        const QFuture<int> item = limitedRun.items.at(input);
        QVERIFY(waitUntil([&]() { return item.isCanceled(); }, kTimeoutMs));
    }
    QVERIFY(waitUntil([&]() { return limitedRun.all.isCanceled(); }, kTimeoutMs));
    QCOMPARE(launcher.order.size(), kOneAtATime);
}

void LimitedTests::test_items_observable_before_admission()
{
    constexpr int itemCount = 3;
    constexpr int last = itemCount - 1;
    Launcher launcher;
    QObject context;

    auto limitedRun = limited(&context, inputRange(itemCount),
                              launcher.launch(), launcher.weight(),
                              Limit{kOneAtATime, kCountOnly});
    tick();

    QCOMPARE(limitedRun.items.size(), itemCount);
    QCOMPARE(launcher.order.size(), kOneAtATime);

    const QFuture<int> lastItem = limitedRun.items.at(last);
    QVERIFY(!isSettled(lastItem));

    Callable<int> observer;
    observe(lastItem).subscribe(observer.func);

    for (int input = 0; input < itemCount; ++input) {
        QVERIFY(waitUntil([&]() { return launcher.order.contains(input); }, kTimeoutMs));
        QVERIFY(!observer.called);
        launcher.complete(input);
    }

    QVERIFY(waitUntil([&]() { return observer.called; }, kTimeoutMs));
    QCOMPARE(observer.value, last + kResultOffset);
}

void LimitedTests::test_all_finishes_after_last_item()
{
    constexpr int itemCount = 3;
    Launcher launcher;
    QObject context;

    auto limitedRun = limited(&context, inputRange(itemCount),
                              launcher.launch(), launcher.weight(),
                              Limit{kOneAtATime, kCountOnly});
    tick();
    QCOMPARE(launcher.order.size(), kOneAtATime);

    // A canceled item counts as settled, and all waits for the rest.
    launcher.launched.at(0).future().cancel();
    tick();
    QVERIFY(waitUntil([&]() { return launcher.order.size() == 2; }, kTimeoutMs));
    QVERIFY(!isSettled(limitedRun.all));

    launcher.complete(1);
    QVERIFY(waitUntil([&]() { return launcher.order.size() == 3; }, kTimeoutMs));
    QVERIFY(!isSettled(limitedRun.all));

    launcher.complete(2);
    QVERIFY(waitUntil(limitedRun.all, kTimeoutMs));
    QVERIFY(!limitedRun.all.isCanceled());
    QVERIFY(limitedRun.items.at(0).isCanceled());
    QCOMPARE(limitedRun.items.at(2).result(), 2 + kResultOffset);
}

void LimitedTests::test_ledger_charged_on_admission_released_on_settle()
{
    constexpr int itemCount = 3;
    constexpr int maxRunning = 10;
    constexpr qint64 itemWeight = 3;
    constexpr qint64 ledgerCap = 7;
    constexpr int fitsInLedger = 2;

    Launcher launcher;
    launcher.weights = QList<qint64>(itemCount, itemWeight);
    FakeLedger ledger(ledgerCap);
    QObject context;

    auto limitedRun = limited(&context, inputRange(itemCount),
                              launcher.launch(), launcher.weight(),
                              Limit{maxRunning, kCountOnly, &ledger});
    tick();

    // The ledger, not the count, holds the third item back.
    QCOMPARE(launcher.order.size(), fitsInLedger);
    QCOMPARE(ledger.charged, fitsInLedger * itemWeight);
    QCOMPARE(ledger.chargeCount, fitsInLedger);
    QVERIFY(ledger.refusedCount > 0);

    launcher.complete(0);
    QVERIFY(waitUntil([&]() { return launcher.order.size() == itemCount; }, kTimeoutMs));
    QCOMPARE(ledger.releaseCount, 1);
    QCOMPARE(ledger.chargeCount, itemCount);
    QCOMPARE(ledger.charged, fitsInLedger * itemWeight);

    launcher.complete(1);
    launcher.complete(2);
    QVERIFY(waitUntil(limitedRun.all, kTimeoutMs));
    QCOMPARE(ledger.releaseCount, itemCount);
    QCOMPARE(ledger.charged, qint64(0));
}

void LimitedTests::test_cancel_admitted_item_propagates_and_frees_slot()
{
    constexpr int itemCount = 2;
    Launcher launcher;
    QObject context;

    auto limitedRun = limited(&context, inputRange(itemCount),
                              launcher.launch(), launcher.weight(),
                              Limit{kOneAtATime, kCountOnly});
    tick();
    QCOMPARE(launcher.order, QList<int>({0}));

    QFuture<int> running = limitedRun.items.at(0);
    running.cancel();

    QVERIFY(waitUntil([&]() { return launcher.launched.at(0).future().isCanceled(); }, kTimeoutMs));
    QVERIFY(waitUntil([&]() { return launcher.order.size() == itemCount; }, kTimeoutMs));
    QCOMPARE(launcher.order, QList<int>({0, 1}));

    launcher.complete(1);
    QVERIFY(waitUntil(limitedRun.all, kTimeoutMs));
    QVERIFY(!limitedRun.all.isCanceled());
    QVERIFY(limitedRun.items.at(0).isCanceled());
    QCOMPARE(limitedRun.items.at(1).result(), 1 + kResultOffset);
}

void LimitedTests::test_ledger_released_on_context_destruction()
{
    constexpr int itemCount = 2;
    constexpr qint64 itemWeight = 3;
    constexpr qint64 ledgerCap = 100;

    Launcher launcher;
    launcher.weights = QList<qint64>(itemCount, itemWeight);
    FakeLedger ledger(ledgerCap);
    DrainEventsOnExit drainBeforeLedgerDies;
    auto context = std::make_unique<QObject>();

    auto limitedRun = limited(context.get(), inputRange(itemCount),
                              launcher.launch(), launcher.weight(),
                              Limit{kManyRunning, kCountOnly, &ledger});
    tick();
    QCOMPARE(launcher.order.size(), itemCount);
    QCOMPARE(ledger.charged, itemCount * itemWeight);

    context.reset();
    tick();

    for (int input = 0; input < itemCount; ++input) {
        launcher.complete(input);
    }

    QVERIFY(waitUntil([&]() { return ledger.releaseCount == itemCount; }, kTimeoutMs));
    QCOMPARE(ledger.charged, qint64(0));
    QVERIFY(waitUntil([&]() { return limitedRun.all.isCanceled(); }, kTimeoutMs));
}

void LimitedTests::test_two_runs_share_one_ledger()
{
    constexpr int itemCount = 2;
    constexpr qint64 ledgerCap = 3;

    ByteLedger ledger(ledgerCap);
    DrainEventsOnExit drainBeforeLedgerDies;
    Launcher launcherA;
    Launcher launcherB;
    QObject contextA;
    QObject contextB;

    auto runA = limited(&contextA, inputRange(itemCount),
                        launcherA.launch(), launcherA.weight(),
                        Limit{kManyRunning, kCountOnly, &ledger});
    auto runB = limited(&contextB, inputRange(itemCount),
                        launcherB.launch(), launcherB.weight(),
                        Limit{kManyRunning, kCountOnly, &ledger});
    tick();

    QCOMPARE(launcherA.order, QList<int>({0, 1}));
    QCOMPARE(launcherB.order, QList<int>({0}));
    QCOMPARE(ledger.charged(), ledgerCap);

    // Run A's release wakes run B, though none of B's items settled.
    launcherA.complete(0);
    QVERIFY(waitUntil([&]() { return launcherB.order.size() == itemCount; }, kTimeoutMs));
    QCOMPARE(launcherB.order, QList<int>({0, 1}));
    QVERIFY(!isSettled(launcherB.launched.at(0).future()));
    QCOMPARE(ledger.charged(), ledgerCap);

    launcherA.complete(1);
    launcherB.complete(0);
    launcherB.complete(1);
    QVERIFY(waitUntil(runA.all, kTimeoutMs));
    QVERIFY(waitUntil(runB.all, kTimeoutMs));
    QCOMPARE(ledger.charged(), qint64(0));
}

void LimitedTests::test_ledger_first_item_charged_past_cap()
{
    constexpr qint64 ledgerCap = 5;
    constexpr qint64 heavyWeight = 10;
    constexpr qint64 lightWeight = 1;
    constexpr int lightCount = 2;

    ByteLedger ledger(ledgerCap);
    DrainEventsOnExit drainBeforeLedgerDies;
    Launcher heavy;
    heavy.weights = {heavyWeight};
    Launcher light;
    light.weights = QList<qint64>(lightCount, lightWeight);
    QObject heavyContext;
    QObject lightContext;

    auto heavyRun = limited(&heavyContext, inputRange(heavy.weights.size()),
                            heavy.launch(), heavy.weight(),
                            Limit{kManyRunning, kCountOnly, &ledger});
    tick();
    QCOMPARE(heavy.order, QList<int>({0}));
    QCOMPARE(ledger.charged(), heavyWeight);

    // The light run's first item runs by the first-item rule; the heavy charge
    // holds back the rest of it.
    auto lightRun = limited(&lightContext, inputRange(lightCount),
                            light.launch(), light.weight(),
                            Limit{kManyRunning, kCountOnly, &ledger});
    tick();
    QCOMPARE(light.order, QList<int>({0}));
    QCOMPARE(ledger.charged(), heavyWeight + lightWeight);

    heavy.complete(0);
    QVERIFY(waitUntil([&]() { return light.order.size() == lightCount; }, kTimeoutMs));
    QCOMPARE(ledger.charged(), lightCount * lightWeight);

    light.complete(0);
    light.complete(1);
    QVERIFY(waitUntil(heavyRun.all, kTimeoutMs));
    QVERIFY(waitUntil(lightRun.all, kTimeoutMs));
    QCOMPARE(ledger.charged(), qint64(0));
}

void LimitedTests::test_empty_inputs()
{
    Launcher launcher;
    QObject context;

    auto limitedRun = limited(&context, QList<int>(),
                              launcher.launch(), launcher.weight(),
                              Limit{kOneAtATime, kCountOnly});

    QVERIFY(limitedRun.items.isEmpty());
    QVERIFY(limitedRun.all.isFinished());
    QVERIFY(!limitedRun.all.isCanceled());
    QVERIFY(launcher.order.isEmpty());
}

void LimitedTests::test_launcher_returns_finished_future()
{
    QList<int> order;
    QObject context;

    auto launchFinished = [&order](int input) {
        order.append(input);
        Deferred<int> defer;
        defer.complete(input + kResultOffset);
        return defer.future();
    };

    auto limitedRun = limited(&context, inputRange(kItemCount), launchFinished,
                              Limit{kOneAtATime, kCountOnly});

    QVERIFY(waitUntil(limitedRun.all, kTimeoutMs));
    QVERIFY(!limitedRun.all.isCanceled());
    QCOMPARE(order, inputRange(kItemCount));
    for (int input = 0; input < kItemCount; ++input) {
        QCOMPARE(limitedRun.items.at(input).result(), input + kResultOffset);
    }
}

void LimitedTests::test_cancel_all_with_running_and_queued()
{
    constexpr int maxRunning = 2;
    Launcher launcher;
    QObject context;

    auto limitedRun = limited(&context, inputRange(kItemCount),
                              launcher.launch(), launcher.weight(),
                              Limit{maxRunning, kCountOnly});
    tick();
    QCOMPARE(launcher.order.size(), maxRunning);

    limitedRun.all.cancel();

    for (int input = 0; input < maxRunning; ++input) {
        const QFuture<int> launched = launcher.launched.at(input).future();
        QVERIFY(waitUntil([&]() { return launched.isCanceled(); }, kTimeoutMs));
    }
    for (const QFuture<int>& item : limitedRun.items) {
        QVERIFY(waitUntil([&]() { return item.isCanceled(); }, kTimeoutMs));
    }

    for (int input = 0; input < maxRunning; ++input) {
        launcher.complete(input);
    }
    tick();
    QCOMPARE(launcher.order.size(), maxRunning);
}

void LimitedTests::test_weight_called_once_per_input()
{
    constexpr int maxRunning = 2;
    Launcher launcher;
    QList<int> weightCalls(kItemCount, 0);
    QObject context;

    auto countingWeight = [&weightCalls](int input) {
        weightCalls.replace(input, weightCalls.at(input) + 1);
        return kDefaultWeight;
    };

    auto limitedRun = limited(&context, inputRange(kItemCount),
                              launcher.launch(), countingWeight,
                              Limit{maxRunning, kCountOnly});
    tick();

    // A full window evaluates no weight for the next item.
    QCOMPARE(launcher.order.size(), maxRunning);
    QCOMPARE(std::accumulate(weightCalls.cbegin(), weightCalls.cend(), 0), maxRunning);

    for (int input = 0; input < kItemCount; ++input) {
        QVERIFY(waitUntil([&]() { return launcher.order.contains(input); }, kTimeoutMs));
        launcher.complete(input);
    }

    QVERIFY(waitUntil(limitedRun.all, kTimeoutMs));
    QCOMPARE(weightCalls, QList<int>(kItemCount, 1));
}

void LimitedTests::test_count_only_overload()
{
    constexpr int maxRunning = 2;
    Launcher launcher;
    QObject context;

    auto limitedRun = limited(&context, inputRange(kItemCount), launcher.launch(),
                              Limit{maxRunning});
    tick();
    QCOMPARE(launcher.order.size(), maxRunning);

    for (int input = 0; input < kItemCount; ++input) {
        QVERIFY(waitUntil([&]() { return launcher.order.contains(input); }, kTimeoutMs));
        QVERIFY(launcher.running() <= maxRunning);
        launcher.complete(input);
    }

    QVERIFY(waitUntil(limitedRun.all, kTimeoutMs));
    QCOMPARE(launcher.peakRunning, maxRunning);
    QCOMPARE(launcher.order, inputRange(kItemCount));
}

void LimitedTests::test_type_deduced_from_launcher()
{
    Launcher weightedLauncher;
    Launcher countOnlyLauncher;
    QObject context;

    auto weighted = limited(&context, inputRange(kOneAtATime),
                            weightedLauncher.launch(), weightedLauncher.weight(),
                            Limit{kOneAtATime, kCountOnly});
    auto countOnly = limited(&context, inputRange(kOneAtATime),
                             countOnlyLauncher.launch(), Limit{kOneAtATime});
    static_assert(std::is_same_v<decltype(weighted), Limited<int>>);
    static_assert(std::is_same_v<decltype(countOnly), Limited<int>>);
    tick();

    weightedLauncher.complete(0);
    countOnlyLauncher.complete(0);
    QVERIFY(waitUntil(weighted.all, kTimeoutMs));
    QVERIFY(waitUntil(countOnly.all, kTimeoutMs));
    QCOMPARE(weighted.items.at(0).result(), kResultOffset);
    QCOMPARE(countOnly.items.at(0).result(), kResultOffset);
}
