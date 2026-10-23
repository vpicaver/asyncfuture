#ifndef LIMITTESTFIXTURES_H
#define LIMITTESTFIXTURES_H

#include <QPromise>
#include <functional>
#include <memory>
#include <vector>
#include <asyncfuture.h>
#include "testfunctions.h"

// Launchers and ledgers for the Limiter tests.
namespace LimitTestFixtures {

constexpr int kTimeoutMs = 1000;
constexpr int kResultOffset = 100;
constexpr qint64 kDefaultWeight = 1;
constexpr int kOneAtATime = 1;
constexpr int kManyRunning = 10;
constexpr qint64 kCountOnly = 0;

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
    QList<AsyncFuture::Deferred<int>> launched;
    int peakRunning = 0;
    qint64 peakWeight = 0;
    QList<qint64> weights;

    int running() const
    {
        int count = 0;
        for (const AsyncFuture::Deferred<int>& defer : launched) {
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
            AsyncFuture::Deferred<int> defer;
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
        AsyncFuture::Deferred<int> defer = launched.at(order.indexOf(input));
        defer.complete(input + kResultOffset);
        Test::tick();
    }
};

// Records every charge and release against a fixed cap.
class FakeLedger : public AsyncFuture::Ledger {
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

// Launches one QPromise per admitted input, so the launched future stays
// unfinished after a cancel until the test finishes it, as a worker would.
class PromiseLauncher {
public:
    QList<int> order;
    std::vector<std::unique_ptr<QPromise<int>>> promises;
    QList<qint64> weights;

    std::function<QFuture<int>(int)> launch()
    {
        return [this](int input) {
            auto promise = std::make_unique<QPromise<int>>();
            promise->start();
            QFuture<int> future = promise->future();
            order.append(input);
            promises.push_back(std::move(promise));
            return future;
        };
    }

    std::function<qint64(int)> weight() const
    {
        return [this](int input) { return weights.at(input); };
    }

    QFuture<int> launched(int input) const
    {
        return promises.at(order.indexOf(input))->future();
    }

    // Ends the worker for input without a result, as a canceled worker exits.
    void finish(int input)
    {
        promises.at(order.indexOf(input))->finish();
        Test::tick();
    }

    void complete(int input)
    {
        QPromise<int>& promise = *promises.at(order.indexOf(input));
        promise.addResult(input + kResultOffset);
        promise.finish();
        Test::tick();
    }
};

// Declared right after a ledger, so a test that fails with items still
// running delivers their releases before the ledger is destroyed.
struct DrainEventsOnExit {
    ~DrainEventsOnExit() { Test::tick(); }
};

} // namespace LimitTestFixtures

#endif // LIMITTESTFIXTURES_H
