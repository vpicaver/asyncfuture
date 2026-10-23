#include "asyncfuture.h"

#include <QMutexLocker>

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

} // namespace AsyncFuture
