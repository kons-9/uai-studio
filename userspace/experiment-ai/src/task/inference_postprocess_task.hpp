#pragma once

namespace uai::ai::task {

class InferencePostprocessTask final {
public:
    static void Entry();

private:
    void Run();
};

} // namespace uai::ai::task
