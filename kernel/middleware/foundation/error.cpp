#include "middleware/foundation/error.hpp"
#include "middleware/foundation/log.hpp"

namespace uai::ai::common {

void Error::LogStatus(const char *component) const
{
    LogStatus(component, IsRoutine() ? LogLevel::kDebug : LogLevel::kError);
}

void Error::LogStatus(
    const char *component,
    LogLevel level
) const
{
    if (Ok())
        return;

    UAI_LOGF(
        level, "error: component=%s code=%s(%u)\n", component, ErrorCodeName(code_), static_cast<unsigned int>(code_)
    );
}

} // namespace uai::ai::common