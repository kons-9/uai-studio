#include "periodic.hpp"
#include <cstdlib>
#include <iostream>

void Require(bool condition)
{
    if (!condition) {
        std::cerr << "periodic check failed\n";
        std::exit(1);
    }
}

int main()
{
    experiment::scheduling::Periodic timer;
    Require(!timer.Configure(0, 0, experiment::scheduling::LatePolicy::kSkip));
    Require(timer.Configure(10, UINT32_MAX - 4, experiment::scheduling::LatePolicy::kLatestOnly));
    Require(timer.Take(UINT32_MAX - 5) == 0);
    Require(timer.Take(UINT32_MAX - 4) == 1);
    Require(timer.Take(5) == 1 && timer.Take(5) == 0);
    Require(timer.Take(36) == 1 && timer.Take(45) == 1);
    Require(timer.Statistics().due == 6 && timer.Statistics().runs == 4 && timer.Statistics().skipped == 2);
    Require(timer.Configure(10, 0, experiment::scheduling::LatePolicy::kCatchUp, 3));
    Require(timer.Take(95) == 3 && timer.Statistics().skipped == 7 && timer.Take(100) == 1);
    Require(timer.Configure(10, 0, experiment::scheduling::LatePolicy::kSkip));
    Require(timer.Take(11) == 0 && timer.Take(20) == 1 && timer.Statistics().skipped == 2);
    Require(timer.Statistics().max_lateness == 11);
}