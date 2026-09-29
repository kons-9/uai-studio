#ifndef UAI_AI_TASK_PERSON_PIPELINE_TASK_HPP
#define UAI_AI_TASK_PERSON_PIPELINE_TASK_HPP

namespace uai::ai::task {

/* Person pipeline tasks. Each worker runs one ai_runtime lane. */
class PersonPipelineTask final {
public:
    static void FrameEntry();
    static void PreprocessEntry();
    static void NpuEntry();
    static void PostprocessEntry();
};

} // namespace uai::ai::task

#endif
