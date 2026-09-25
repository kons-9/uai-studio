#ifndef UAI_SAMPLE2_MODEL_API_HPP
#define UAI_SAMPLE2_MODEL_API_HPP

#include "stai.h"

namespace uai::sample2::model_manager {

/* C++側が扱うモデルの共通インターフェース。モデル固有のSTAI APIや
 * Cリンケージは、各モデルのアダプターに隠す。 */
class Model {
public:
    virtual stai_return_code Initialize() = 0;
    virtual stai_return_code Shutdown() = 0;
    virtual stai_return_code GetInfo(stai_network_info *info) = 0;
    virtual stai_return_code GetInputs(stai_ptr *inputs, stai_size *count) = 0;
    virtual stai_return_code GetOutputs(stai_ptr *outputs, stai_size *count) = 0;
    virtual stai_return_code Run(stai_run_mode mode) = 0;
    virtual stai_return_code ContinueRun() = 0;
    virtual stai_return_code WaitForEvent() = 0;
    virtual stai_return_code GetRunStatus() = 0;
    virtual stai_return_code NewInference() = 0;

protected:
    /* Model instances are owned by their concrete adapter, never deleted
     * through this interface. */
    ~Model() = default;
};

} // namespace uai::sample2::model_manager

#endif
