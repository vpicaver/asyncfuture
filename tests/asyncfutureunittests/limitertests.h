#ifndef LIMITERTESTS_H
#define LIMITERTESTS_H

#include <QObject>

class LimiterTests : public QObject
{
    Q_OBJECT
public:
    explicit LimiterTests(QObject *parent = nullptr);

private slots:
    void test_item_waits_for_running_item();
    void test_max_running_holds_as_items_arrive();
    void test_weight_budget_holds_until_enough_weight_returns();
    void test_first_item_runs_past_budget();
    void test_ledger_charged_on_admission_released_on_finish();
    void test_release_from_another_limiter_wakes_admission();
    void test_cancel_queued_item_skips_it();
    void test_cancel_admitted_item_propagates_and_holds_slot();
    void test_destroying_limiter_cancels_queued_and_running_settles();
    void test_counts_follow_every_transition();
    void test_admit_from_inside_launcher_queues_behind_running_item();
    void test_throwing_launcher_cancels_item_with_ledger_and_counts_unchanged();
    void test_launcher_throwing_inside_another_callers_admit_cancels_only_its_item();
    void test_cancel_queued_cancels_waiting_items_and_keeps_running_one();
    void test_queued_count_drops_canceled_item_before_it_reaches_front();
    void test_release_on_worker_thread_wakes_main_thread_limiter();
    void test_fifo_holds_behind_item_that_does_not_fit();
    void test_launcher_throwing_after_release_cancels_item_and_queue_moves_on();
    void test_limited_launcher_throwing_after_release_cancels_item_and_run_completes();
};

#endif // LIMITERTESTS_H
