#ifndef LIMITEDTESTS_H
#define LIMITEDTESTS_H

#include <QObject>

class LimitedTests : public QObject
{
    Q_OBJECT
public:
    explicit LimitedTests(QObject *parent = nullptr);

private slots:
    void test_count_limit_holds();
    void test_weight_limit_holds();
    void test_first_item_runs_past_budget();
    void test_fifo_order();
    void test_launch_count_equals_admitted_count();
    void test_cancel_all_before_admission();
    void test_cancel_one_before_admission();
    void test_context_destroyed_mid_run();
    void test_items_observable_before_admission();
    void test_all_finishes_after_last_item();
    void test_ledger_charged_on_admission_released_on_settle();
};

#endif // LIMITEDTESTS_H
