#include "hwtest.hpp"
#include "driver/peripheral_driver/peripheral_driver.hpp"

namespace experiment::hwtest::tests::rtc_driver {

Result Run(const Context &context)
{
    uai::ai::peripheral::PeripheralManagement::Accessor accessor;
    if (!uai::ai::peripheral::PeripheralManagement::Instance().Acquire(&accessor, 100).Ok()) {
        return {Outcome::kFail, "rtc-ownership"};
    }
    if (!accessor->OpenCalendar(accessor.Ownership()).Ok()
        || !accessor->SetCalendar({25, 1, 1, 3, 23, 59, 59}, accessor.Ownership()).Ok()) {
        return {Outcome::kFail, "rtc-initialization-or-calendar-write"};
    }
    Result result{Outcome::kFail, "rtc-midnight-rollover-timeout"};
    const auto begin = context.clock();
    while (!context.Expired(begin, 2500)) {
        uai::ai::peripheral::Calendar calendar{};
        if (!accessor->ReadCalendar(&calendar, accessor.Ownership()).Ok()) {
            result = {Outcome::kFail, "rtc-calendar-read"};
            break;
        }
        if (calendar.hours == 0 && calendar.minutes == 0 && calendar.seconds <= 1 && calendar.year == 25
            && calendar.month == 1 && calendar.day == 2 && calendar.weekday == 4) {
            result = {Outcome::kPass, "lsi-calendar-midnight-date-rollover"};
            break;
        }
        context.wait(10);
    }
    return accessor.Close().Ok() ? result : Result{Outcome::kFail, "rtc-deinitialization"};
}

}