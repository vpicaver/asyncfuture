#ifndef SETTLEDTESTS_H
#define SETTLEDTESTS_H

#include <QObject>

class SettledTests : public QObject
{
    Q_OBJECT
public:
    explicit SettledTests(QObject *parent = nullptr);

private slots:
    void test_complete_settled_future_is_immediate();
    void test_complete_settled_future_keeps_result();
    void test_complete_settled_future_keeps_progress();
    void test_complete_settled_future_observer_is_async();
    void test_complete_canceled_future_is_immediate();
    void test_complete_canceled_future_keeps_progress();
    void test_complete_settled_then_cancel_stays_completed();
    void test_complete_settled_then_cancel_is_not_timing_dependent();
    void test_complete_settled_future_handle_stays_cancelable();
    void test_complete_settled_future_does_not_propagate_cancel();
    void test_complete_settled_future_off_thread();

    void test_combinator_stream_still_defers();
    void test_combinator_stream_is_not_premature();

    void test_combine_set_is_immediate();
    void test_combine_set_waits_for_pending();
    void test_combine_set_fail_fast_cancels();
    void test_combine_set_canceled_input_is_immediate();
    void test_combine_set_all_settled_completes();
    void test_combine_set_observer_is_async();
    void test_combine_list_is_immediate();
    void test_combine_list_of_typed_futures();
    void test_combine_empty_set_completes();
    void test_combine_set_keeps_settled_progress();
    void test_combine_set_all_settled_waits_for_canceled_input();
    void test_combine_set_off_thread();
    void test_combine_set_nested_is_immediate();
};

#endif // SETTLEDTESTS_H
