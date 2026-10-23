#include <QTest>
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

    void release(qint64 bytes) override
    {
        charged -= bytes;
        ++releaseCount;
    }

    qint64 cap = 0;
    qint64 charged = 0;
    int chargeCount = 0;
    int releaseCount = 0;
    int refusedCount = 0;
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

    auto limitedRun = limited<int>(&context, inputRange(kItemCount),
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

    auto limitedRun = limited<int>(&context, inputRange(kItemCount),
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

    auto limitedRun = limited<int>(&context, inputRange(launcher.weights.size()),
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

    auto limitedRun = limited<int>(&context, inputRange(launcher.weights.size()),
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

    auto limitedRun = limited<int>(&context, inputRange(kItemCount),
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

    auto limitedRun = limited<int>(&context, inputRange(kItemCount),
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

    auto limitedRun = limited<int>(&context, inputRange(itemCount),
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

    auto limitedRun = limited<int>(context.get(), inputRange(kItemCount),
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

    auto limitedRun = limited<int>(&context, inputRange(itemCount),
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

    auto limitedRun = limited<int>(&context, inputRange(itemCount),
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

    auto limitedRun = limited<int>(&context, inputRange(itemCount),
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
