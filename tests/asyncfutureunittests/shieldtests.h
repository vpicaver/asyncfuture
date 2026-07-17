#ifndef SHIELDTESTS_H
#define SHIELDTESTS_H

#include <QObject>

class ShieldTests : public QObject
{
    Q_OBJECT
public:
    explicit ShieldTests(QObject *parent = nullptr);

private slots:
    void test_shield_forwards_result();
    void test_shield_forwards_upstream_cancel();
    void test_shield_blocks_downstream_cancel();
    void test_shield_shared_fanout();
    void test_shield_forwards_progress();
    void test_shield_thread_hop();
    void test_complete_policy_parity();
};

#endif // SHIELDTESTS_H
