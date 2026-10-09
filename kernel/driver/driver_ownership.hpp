#pragma once

#include "middleware/resource_ownership/resource_ownership.hpp"
#include "middleware/resource_ownership/utkernel_mutex_backend.hpp"

namespace uai::ai::driver {

/* Every driver guards its hardware state machine with one of these. The
 * Writer is the transferable right to change that state. */
using ResourceManagement = resource_ownership::ResourceOwnership<resource_ownership::MicroTKernelMutexBackend>;

template <typename Resource>
using ResourceAccessor = resource_ownership::ResourceAccessor<Resource>;

} // namespace uai::ai::driver
