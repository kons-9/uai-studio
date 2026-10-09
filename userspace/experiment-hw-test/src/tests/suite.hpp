#pragma once

#include "hwtest.hpp"

namespace experiment::hwtest::tests {

extern const Case cases[];
extern const std::size_t case_count;

namespace rng_driver {
Result Run(const Context &);
}
namespace hash_driver {
Result Run(const Context &);
}
namespace crc_driver {
Result Run(const Context &);
}
namespace gpdma_driver {
Result Run(const Context &);
}
namespace hpdma_driver {
Result Run(const Context &);
}
namespace rtc_driver {
Result Run(const Context &);
}
namespace tim_driver {
Result Run(const Context &);
}
namespace sram_driver {
Result Run(const Context &);
}
namespace psram_driver {
Result Run(const Context &);
}
namespace nor_driver {
Result Run(const Context &);
}
namespace dma2d_driver {
Result Run(const Context &);
}

}