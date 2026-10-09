#include "operations.hpp"
#include "verification.hpp"
#include <array>
#include <cstdlib>
#include <iostream>

void Require(bool condition)
{
    if (!condition) {
        std::cerr << "blit check failed\n";
        std::exit(1);
    }
}

class SoftwareDevice {
public:
    enum class Fault {
        kNone,
        kNoWrite,
        kGuard,
        kPadding,
        kInput,
        kWrongColor
    };
    Fault fault = Fault::kNone;
    bool
    Run(const experiment::graphics::Request &request,
        std::uint32_t)
    {
        if (!experiment::graphics::Validate(request) || request.operation == experiment::graphics::Operation::kResize
            || reinterpret_cast<std::uintptr_t>(request.destination.data) % 32 != 0) {
            return false;
        }
        if (fault == Fault::kNoWrite) {
            return true;
        }
        if (!experiment::graphics::Reference(request)) {
            return false;
        }
        if (fault == Fault::kGuard) {
            request.destination.data[-1] ^= 1;
        }
        if (fault == Fault::kPadding) {
            request.destination
                .data[request.destination.width * experiment::graphics::PixelBytes(request.destination.format)] ^= 1;
        }
        if (fault == Fault::kInput) {
            request.source.data[0] ^= 1;
        }
        if (fault == Fault::kWrongColor) {
            request.destination.data[0] ^= 0xff;
        }
        return true;
    }
};

struct CacheProbe {
    inline static unsigned prepared = 0, inspected = 0;
    static void Prepare(
        void *address,
        std::int32_t bytes
    )
    {
        Require(reinterpret_cast<std::uintptr_t>(address) % 32 == 0 && bytes == 64 * 32 * 3 + 64);
        Require(inspected == 0);
        ++prepared;
    }
    static void Inspect(
        void *,
        std::int32_t bytes
    )
    {
        Require(prepared == 3 && bytes == 64 * 32 * 3 + 64);
        ++inspected;
    }
};

int main()
{
    std::array<std::uint8_t, 18> pixels{255, 0, 0, 0, 255, 0, 77, 77, 77, 0, 0, 255, 255, 255, 255, 77, 77, 77};
    std::array<std::uint8_t, 12> output;
    output.fill(0xa5);
    experiment::graphics::Image source{pixels.data(), pixels.size(), 2, 2, 9, experiment::graphics::Format::kRgb888};
    experiment::graphics::Image destination{
        output.data(), output.size(), 2, 2, 6, experiment::graphics::Format::kRgb565
    };
    experiment::graphics::Transfer transfer{};
    Require(experiment::graphics::BuildTransfer(source, destination, transfer));
    Require(
        transfer.input_offset == 1 && transfer.output_offset == 1
        && transfer.mode == experiment::graphics::Mode::kConvert
    );
    Require(experiment::graphics::ReferenceBlit(source, destination));
    Require(output == std::array<std::uint8_t, 12>{0, 0xf8, 0xe0, 7, 0xa5, 0xa5, 0x1f, 0, 0xff, 0xff, 0xa5, 0xa5});
    std::array<std::uint8_t, 18> restored{};
    experiment::graphics::Image reverse{
        restored.data(), restored.size(), 2, 2, 9, experiment::graphics::Format::kRgb888
    };
    Require(experiment::graphics::ReferenceBlit(destination, reverse));
    Require(restored[0] == 255 && restored[4] == 255 && restored[11] == 255 && restored[12] == 255);
    destination.bytes = 9;
    Require(!experiment::graphics::BuildTransfer(source, destination, transfer));
    destination.bytes = output.size();
    destination.stride = 5;
    Require(!experiment::graphics::BuildTransfer(source, destination, transfer));
    destination.stride = 6;
    destination.width = 1;
    Require(!experiment::graphics::BuildTransfer(source, destination, transfer));
    Require(!experiment::graphics::BuildTransfer(source, source, transfer));
    source.height = UINT32_MAX;
    Require(!experiment::graphics::Valid(source));
    source.height = 2;
    destination.width = 2;
    Require(experiment::graphics::Reference({experiment::graphics::Operation::kFill, {}, {}, destination, 0xff0000}));
    Require(output[0] == 0 && output[1] == 0xf8 && output[4] == 0xa5);
    Require(
        experiment::graphics::Reference({experiment::graphics::Operation::kBlend, source, reverse, destination, 0, 0})
    );
    Require(output[6] == 0x1f && output[7] == 0);
    destination.width = 1;
    destination.height = 1;
    Require(experiment::graphics::Reference({experiment::graphics::Operation::kResize, source, {}, destination}));
    Require(output[0] == 0xff && output[1] == 0xff);
    Require(!experiment::graphics::Reference({experiment::graphics::Operation::kResize, source, {}, source}));
    experiment::graphics::Verification verification;
    SoftwareDevice device;
    std::uint32_t clock = UINT32_MAX - 2;
    const auto cycles = [&clock] {
        clock += 5;
        return clock;
    };
    for (const auto &test : experiment::graphics::kCases) {
        const auto result = verification.Run(test, device, cycles);
        Require(result.passed && result.cycles == 5 && result.maximum_error == 0 && result.corrupted_bytes == 0);
    }
    for (const auto fault :
         {SoftwareDevice::Fault::kNoWrite,
          SoftwareDevice::Fault::kGuard,
          SoftwareDevice::Fault::kPadding,
          SoftwareDevice::Fault::kInput,
          SoftwareDevice::Fault::kWrongColor}) {
        device.fault = fault;
        Require(!verification.Run(experiment::graphics::kCases[2], device, cycles).passed);
    }
    device.fault = SoftwareDevice::Fault::kGuard;
    Require(!verification.Run(experiment::graphics::kCases[17], device, cycles).passed);
    Require(!verification
                 .Run(experiment::graphics::kCases[17], device, cycles, {CacheProbe::Prepare, CacheProbe::Inspect})
                 .passed);
    Require(CacheProbe::prepared == 3 && CacheProbe::inspected == 3);
}