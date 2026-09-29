#pragma once

namespace uai::ai::task {

/* Camera frames are claimed and prepared here, never in the NPU owner's
 * polling/IRQ continuation loop. Only an explicitly requested model is used. */
class InputPreparationTask final {
public:
    static void Entry();

private:
    void Run();
};

} // namespace uai::ai::task