#ifndef UAI_AI_TASK_PERSON_PIPELINE_TASK_HPP
#define UAI_AI_TASK_PERSON_PIPELINE_TASK_HPP

namespace uai::ai::task {

/* Opt-in person-only application. Each entry runs a different ai_runtime lane. */
class PersonPipelineTask final {
public:
    static void PreprocessEntry();
    static void NpuEntry();
    static void PostprocessEntry();
};

} // namespace uai::ai::task

#endif
