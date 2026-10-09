#include "app_state.hpp"
#include <cstdlib>
#include <deque>
void Require(bool condition) { if (!condition) { std::exit(1); } }
struct Port : experiment::ui::Backend {
    enum class Outcome { kSuccess, kPartialFailure, kUnchangedFailure };
    unsigned calls = 0;
    experiment::features::State actual{};
    std::deque<Outcome> outcomes;
    bool Apply(experiment::features::State next) override
    {
        ++calls;
        const auto outcome = outcomes.empty() ? Outcome::kSuccess : outcomes.front();
        if (!outcomes.empty()) { outcomes.pop_front(); }
        if (outcome != Outcome::kUnchangedFailure) { actual = next; }
        return outcome == Outcome::kSuccess;
    }
};
void TestGuardsAndCommit()
{
    Port port; experiment::ui::AppState state(port);
    Require(!state.Dispatch(experiment::features::Action::kShowPipe2) && port.calls == 0);
    const experiment::features::Action rejected[] = {
        experiment::features::Action::kToggleBoxes,
        experiment::features::Action::kShowPipe2,
        experiment::features::Action::kToggleBoxes
    };
    Require(!state.Transact(rejected, 3) && port.calls == 0 && state.Features().index == 0);
    Require(state.Render().transaction == experiment::ui::TransactionStatus::kRejected && !state.Render().error);
    Require(!state.Transact(nullptr, 0) && port.calls == 0);
    Require(state.Transact(rejected, 2) && port.calls == 1 && state.Render().updates == 1);
    Require(port.actual.index == state.Features().index);
    Require(state.Render().transaction == experiment::ui::TransactionStatus::kCommitted);
    Require(std::strcmp(experiment::features::Value(state.Features(), 0), "pipe2") == 0);
}
void TestRollback()
{
    Port port; experiment::ui::AppState state(port);
    port.outcomes = {Port::Outcome::kPartialFailure, Port::Outcome::kSuccess};
    Require(!state.Dispatch(experiment::features::Action::kToggleBoxes) && port.calls == 2);
    Require(state.Features().index == 0 && port.actual.index == 0 && state.Render().updates == 0);
    Require(state.Render().error && !state.Render().faulted);
    Require(state.Render().transaction == experiment::ui::TransactionStatus::kRolledBack);
    Require(state.Dispatch(experiment::features::Action::kToggleBoxes));
    Require(state.Render().updates == 1 && !state.Render().error);
}
void TestRollbackFailureAndRecovery()
{
    Port port; experiment::ui::AppState state(port);
    Require(state.Publish({0, 1}, {0, 0, 20, 20}, 100) && state.Visible(1));
    port.outcomes = {Port::Outcome::kPartialFailure, Port::Outcome::kUnchangedFailure,
                     Port::Outcome::kUnchangedFailure, Port::Outcome::kSuccess};
    Require(!state.Dispatch(experiment::features::Action::kToggleBoxes) && port.calls == 2);
    Require(state.Features().index == 0 && port.actual.index != 0 && state.Render().updates == 0);
    Require(state.Render().faulted && state.Render().error && !state.Visible(1));
    Require(state.Render().transaction == experiment::ui::TransactionStatus::kFaulted);
    Require(!state.Dispatch(experiment::features::Action::kToggleBoxes) && port.calls == 2);
    state.Touch(true, 0);
    Require(!state.Touch(false, 0) && port.calls == 2);
    Require(!state.Recover() && state.Render().faulted && port.calls == 3);
    Require(state.Recover() && !state.Render().faulted && !state.Render().error && port.calls == 4);
    Require(state.Features().index == 0 && port.actual.index == 0 && state.Render().updates == 0 && state.Visible(1));
    Require(state.Dispatch(experiment::features::Action::kToggleBoxes));
}
void TestResultAndTouch()
{
    Port port; experiment::ui::AppState state(port);
    Require(experiment::features::Value({999}, 0) == nullptr);
    Require(state.Publish({UINT32_MAX - 5, 1}, {0, 0, 20, 20}, 10));
    Require(!state.Publish({0, 1}, {0, 0, 20, 20}, 10));
    Require(state.Visible(3) && !state.Visible(4));
    state.Touch(true, 0); state.Touch(true, -1);
    Require(!state.Touch(false, 0) && state.Render().pressed == -1);
    state.Touch(true, 0);
    Require(state.Touch(false, 0));
}
int main()
{
    TestGuardsAndCommit();
    TestRollback();
    TestRollbackFailureAndRecovery();
    TestResultAndTouch();
}