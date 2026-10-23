#include "asyncfuture.h"

#include <QMutexLocker>

#include <algorithm>
#include <deque>
#include <vector>

namespace AsyncFuture {

Ledger::Ledger(QObject* parent) :
    QObject(parent)
{
}

ByteLedger::ByteLedger(qint64 cap, QObject* parent) :
    Ledger(parent),
    m_cap(cap)
{
}

bool ByteLedger::tryCharge(qint64 bytes)
{
    if (bytes <= 0) {
        return true;
    }
    QMutexLocker locker(&m_mutex);
    if (m_charged + bytes > m_cap) {
        return false;
    }
    m_charged += bytes;
    return true;
}

void ByteLedger::charge(qint64 bytes)
{
    if (bytes <= 0) {
        return;
    }
    QMutexLocker locker(&m_mutex);
    m_charged += bytes;
}

void ByteLedger::release(qint64 bytes)
{
    if (bytes <= 0) {
        return;
    }
    {
        QMutexLocker locker(&m_mutex);
        m_charged -= bytes;
    }
    emit released();
}

qint64 ByteLedger::cap() const
{
    QMutexLocker locker(&m_mutex);
    return m_cap;
}

qint64 ByteLedger::charged() const
{
    QMutexLocker locker(&m_mutex);
    return m_charged;
}

namespace Private {

/* The admission state a Limiter shares with the watchers of its running items,
 * so each running item still returns its slot and ledger bytes after the
 * Limiter is destroyed.
 */
class LimiterCore : public std::enable_shared_from_this<LimiterCore> {
public:
    LimiterCore(Limiter* owner, Limit limit) :
        owner(owner),
        limit(limit)
    {
    }

    bool fitsBudget(qint64 itemWeight) const
    {
        return limit.budget <= 0 || inFlight + itemWeight <= limit.budget;
    }

    // The first item of a window is charged in full whether or not it fits, so
    // every Limiter sharing the ledger sees it; any other item is charged only
    // when it fits.
    bool chargeLedger(LimiterItem& item, qint64 itemWeight, bool isFirst)
    {
        if (limit.ledger == nullptr) {
            return true;
        }
        if (isFirst) {
            limit.ledger->charge(itemWeight);
        } else if (!limit.ledger->tryCharge(itemWeight)) {
            return false;
        }
        item.ledgerCharge = itemWeight;
        return true;
    }

    void releaseLedger(LimiterItem& item)
    {
        const qint64 charge = item.ledgerCharge;
        if (charge > 0) {
            item.ledgerCharge = 0;
            limit.ledger->release(charge);
        }
    }

    // A weight function or launcher that throws cancels its item with the
    // ledger charge returned, and the loop moves on; nothing is rethrown.
    void admit()
    {
        // A launcher that settles synchronously or admits another item, or a
        // ledger release on this thread, re-enters here; the loop below already
        // continues with the next item.
        if (admitting || owner == nullptr) {
            return;
        }
        // A launcher may destroy the Limiter.
        const std::shared_ptr<LimiterCore> self = shared_from_this();
        admitting = true;
        const ResetFlag resetAdmitting{admitting};
        while (!queued.empty()) {
            const std::shared_ptr<LimiterItem> item = queued.front();
            if (item->isSettled()) {
                // Canceled before its turn.
                item->cancel();
                queued.pop_front();
                item->settle();
                continue;
            }

            // The first item of a window runs whatever its weight.
            const bool isFirst = running.empty();
            if (!isFirst && static_cast<qsizetype>(running.size()) >= limit.maxRunning) {
                break;
            }

            qint64 itemWeight = 0;
            try {
                itemWeight = item->weight();
                if (!isFirst && !fitsBudget(itemWeight)) {
                    break;
                }
                if (!chargeLedger(*item, itemWeight, isFirst)) {
                    break;
                }

                // Admitted before the launcher runs, so canceling the queue from
                // inside it leaves this item to settle when its launched future finishes.
                queued.pop_front();
                item->launch();
            } catch (...) {
                const auto found = std::find(queued.begin(), queued.end(), item);
                if (found != queued.end()) {
                    queued.erase(found);
                }
                releaseLedger(*item);
                item->cancel();
                item->settle();
                continue;
            }
            running.push_back(item);
            inFlight += itemWeight;

            item->forwardLaunched();

            item->whenLaunchedFinished(owner, [self, item]() {
                self->release(item);
            });
        }
    }

    // Runs once per admitted item, from the Limiter's watcher or, after the
    // Limiter is gone, from the fallback watcher.
    void release(const std::shared_ptr<LimiterItem>& item)
    {
        const auto found = std::find(running.begin(), running.end(), item);
        if (found == running.end()) {
            return;
        }
        running.erase(found);
        // Counters first: returning the ledger's bytes can re-enter admit()
        // through released().
        inFlight -= item->weight();
        releaseLedger(*item);
        item->settle();
        admit();
    }

    void cancelQueued()
    {
        // Settling can re-enter, so the queue is emptied first.
        std::deque<std::shared_ptr<LimiterItem>> canceled;
        canceled.swap(queued);
        for (const std::shared_ptr<LimiterItem>& item : canceled) {
            item->cancel();
            item->settle();
        }
    }

    // The Limiter's watchers die with it, so each running item settles, and
    // returns its ledger bytes, when its launched work finishes.
    void close()
    {
        QObject::disconnect(onLedgerReleased);
        cancelQueued();
        owner = nullptr;
        const std::shared_ptr<LimiterCore> self = shared_from_this();
        for (const std::shared_ptr<LimiterItem>& item : running) {
            item->whenLaunchedFinished(nullptr, [self, item]() {
                self->release(item);
            });
        }
    }

    Limiter* owner = nullptr; // null once the Limiter is destroyed
    Limit limit;
    std::deque<std::shared_ptr<LimiterItem>> queued;
    std::vector<std::shared_ptr<LimiterItem>> running; // admitted, launched future unfinished
    QMetaObject::Connection onLedgerReleased;
    qint64 inFlight = 0;
    bool admitting = false;
};

} // namespace Private

Limiter::Limiter(Limit limit, QObject* parent) :
    QObject(parent),
    m_core(std::make_shared<Private::LimiterCore>(this, limit))
{
    Q_ASSERT(limit.maxRunning >= 1);
    if (limit.ledger != nullptr) {
        const std::weak_ptr<Private::LimiterCore> core = m_core;
        m_core->onLedgerReleased = connect(limit.ledger, &Ledger::released, this, [core]() {
            if (auto lockedCore = core.lock()) {
                lockedCore->admit();
            }
        });
    }
}

Limiter::~Limiter()
{
    m_core->close();
}

void Limiter::enqueue(std::shared_ptr<Private::LimiterItem> item)
{
    m_core->queued.push_back(std::move(item));
    m_core->admit();
}

void Limiter::cancelQueued()
{
    m_core->cancelQueued();
}

int Limiter::queuedCount() const
{
    const auto& queued = m_core->queued;
    return static_cast<int>(std::count_if(queued.begin(), queued.end(),
                                          [](const std::shared_ptr<Private::LimiterItem>& item) {
        return !item->isSettled();
    }));
}

int Limiter::runningCount() const
{
    return static_cast<int>(m_core->running.size());
}

const Limit& Limiter::limit() const
{
    return m_core->limit;
}

} // namespace AsyncFuture
