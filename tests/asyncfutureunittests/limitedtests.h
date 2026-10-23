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
    void test_cancel_admitted_item_propagates_and_frees_slot();
    void test_ledger_released_on_context_destruction();
    void test_cancel_keeps_ledger_until_launched_future_finishes();
    void test_context_destruction_keeps_ledger_until_launched_future_finishes();
    void test_all_waits_for_canceled_item_to_finish();
    void test_cancel_settles_all_only_after_workers_exit();
    void test_context_destruction_settles_all_only_after_workers_exit();
    void test_all_completes_when_run_finishes_normally();
    void test_direct_all_cancel_still_drains();
    void test_cancel_is_safe_on_a_copy_and_after_completion();
    void test_cancel_from_inside_launcher_waits_for_launched_worker();
    void test_two_runs_share_one_ledger();
    void test_ledger_first_item_charged_past_cap();
    void test_empty_inputs();
    void test_launcher_returns_finished_future();
    void test_cancel_all_with_running_and_queued();
    void test_weight_called_once_per_input();
    void test_count_only_overload();
    void test_type_deduced_from_launcher();
};

#endif // LIMITEDTESTS_H
