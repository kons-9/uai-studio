#include "model_manager/model_manager.hpp"

#include <cstddef>
#include <cstdint>

#include <tk/tkernel.h>

#if defined(AI_MODEL_FACE) || defined(AI_DYNAMIC_MODEL_SWITCHING)
#include "model_manager/model/face/model_face_postprocess.h"
#endif

#include "driver/npu_driver/debug.h"

/* C実装のT-Monitor APIをC++から呼び出すためのCリンケージ。 */
extern "C" {
#include <tm/tmonitor.h>
}

#if AI_INFERENCE_DIAGNOSTICS
#define AI_INFERENCE_TRACE(...) tm_printf(__VA_ARGS__)
#else
#define AI_INFERENCE_TRACE(...) ((void)0)
#endif

namespace uai::ai {

using common::Error;
using common::ErrorCode;

namespace {

using memory_allocator::BoxSet;
using memory_allocator::DetectionSet;
using memory_allocator::SegmentationSet;
using Float = float;

#if AI_INFERENCE_FPS_DIAGNOSTICS
std::uint32_t DiagnosticNow()
{
    SYSTIM time = {};
    return tk_get_otm(&time) == E_OK ? time.lo : 0U;
}
#endif

#if defined(AI_MODEL_PERSON) || defined(AI_DYNAMIC_MODEL_SWITCHING)
constexpr std::size_t kInputSize = 480U;
constexpr std::uint32_t kMaxDetections = 100U;

struct OdInput {
    void *raw_l = nullptr;
    void *raw_m = nullptr;
    void *raw_s = nullptr;
};

struct OdDetection {
    Float x_center = 0.0F;
    Float y_center = 0.0F;
    Float width = 0.0F;
    Float height = 0.0F;
    Float confidence = 0.0F;
    std::int32_t class_index = 0;
};

struct OdOutput {
    OdDetection *detections = nullptr;
    std::int32_t count = 0;
};

struct OdParams {
    std::int32_t nb_classes = 0;
    std::int32_t nb_anchors = 0;
    std::int32_t grid_width_l = 0;
    std::int32_t grid_height_l = 0;
    std::int32_t grid_width_m = 0;
    std::int32_t grid_height_m = 0;
    std::int32_t grid_width_s = 0;
    std::int32_t grid_height_s = 0;
    std::int32_t nb_input_boxes = 0;
    std::int32_t max_boxes_limit = 0;
    Float conf_threshold = 0.0F;
    Float iou_threshold = 0.0F;
    const Float *anchors_l = nullptr;
    const Float *anchors_m = nullptr;
    const Float *anchors_s = nullptr;
    std::int32_t nb_detect = 0;
    Float raw_l_scale = 0.0F;
    Float raw_m_scale = 0.0F;
    Float raw_s_scale = 0.0F;
    std::int8_t raw_l_zero_point = 0;
    std::int8_t raw_m_zero_point = 0;
    std::int8_t raw_s_zero_point = 0;
};

/* C実装のYOLOX後処理ライブラリの関数宣言。定義はSTの
 * lib_vision_models_pp (od_pp_st_yolox.c) 側にある。 */
extern "C" {
std::int32_t od_st_yolox_pp_reset(OdParams *params);
std::int32_t od_st_yolox_pp_process_int8(OdInput *input, OdOutput *output,
                                         OdParams *params);
}

OdParams g_postprocess{};
OdDetection g_postprocess_buffer[kMaxDetections]{};
std::size_t g_output_order[3]{};
const Float g_anchors_l[6] = {30.0F, 30.0F, 4.2F, 15.0F, 13.8F, 42.0F};
const Float g_anchors_m[6] = {15.0F, 15.0F, 2.1F, 7.5F, 6.9F, 21.0F};
const Float g_anchors_s[6] = {7.5F, 7.5F, 1.05F, 3.75F, 3.45F, 10.5F};

void SortOutputs(const stai_network_info &info)
{
    for (std::size_t i = 0U; i < 3U; ++i) {
        g_output_order[i] = i;
    }
    for (std::size_t i = 1U; i < 3U; ++i) {
        const std::size_t value = g_output_order[i];
        std::size_t j = i;
        while (j > 0U &&
               info.outputs[g_output_order[j - 1U]].size_bytes >
                   info.outputs[value].size_bytes) {
            g_output_order[j] = g_output_order[j - 1U];
            --j;
        }
        g_output_order[j] = value;
    }
}

bool InitializePostprocess(const stai_network_info &info)
{
    if (info.n_outputs != 3U || info.outputs == nullptr) {
        return false;
    }
    SortOutputs(info);
    g_postprocess = {};
    g_postprocess.nb_classes = 1;
    g_postprocess.nb_anchors = 3;
    g_postprocess.grid_width_l = 60;
    g_postprocess.grid_height_l = 60;
    g_postprocess.grid_width_m = 30;
    g_postprocess.grid_height_m = 30;
    g_postprocess.grid_width_s = 15;
    g_postprocess.grid_height_s = 15;
    g_postprocess.max_boxes_limit = static_cast<std::int32_t>(kMaxDetections);
    g_postprocess.conf_threshold = 0.6F;
    g_postprocess.iou_threshold = 0.5F;
    g_postprocess.anchors_l = g_anchors_l;
    g_postprocess.anchors_m = g_anchors_m;
    g_postprocess.anchors_s = g_anchors_s;
    g_postprocess.raw_s_scale =
        info.outputs[g_output_order[0]].scale.data[0];
    g_postprocess.raw_s_zero_point = static_cast<std::int8_t>(
        info.outputs[g_output_order[0]].zeropoint.data[0]);
    g_postprocess.raw_m_scale =
        info.outputs[g_output_order[1]].scale.data[0];
    g_postprocess.raw_m_zero_point = static_cast<std::int8_t>(
        info.outputs[g_output_order[1]].zeropoint.data[0]);
    g_postprocess.raw_l_scale =
        info.outputs[g_output_order[2]].scale.data[0];
    g_postprocess.raw_l_zero_point = static_cast<std::int8_t>(
        info.outputs[g_output_order[2]].zeropoint.data[0]);
    return od_st_yolox_pp_reset(&g_postprocess) == 0;
}

std::int16_t ClampCoordinate(Float value, std::int32_t limit)
{
    if (value <= 0.0F) {
        return 0;
    }
    if (value >= static_cast<Float>(limit)) {
        return static_cast<std::int16_t>(limit);
    }
    return static_cast<std::int16_t>(value);
}

bool ConvertDetections(stai_ptr *outputs, DetectionSet *result)
{
    OdInput input{};
    input.raw_s = outputs[g_output_order[0]];
    input.raw_m = outputs[g_output_order[1]];
    input.raw_l = outputs[g_output_order[2]];
    OdOutput output{g_postprocess_buffer, 0};
    /* The reference wrapper resets nb_detect before every run. The decoder
     * appends detections starting at this index, so retaining it carries old
     * boxes into the next inference (especially visible when exposure changes). */
    g_postprocess.nb_detect = 0;
    if (od_st_yolox_pp_process_int8(&input, &output, &g_postprocess) != 0) {
        return false;
    }

    const std::uint32_t count = output.count > 0
                                    ? static_cast<std::uint32_t>(output.count)
                                    : 0U;
    result->count = count < memory_allocator::kMaxBoxes
                        ? count
                        : memory_allocator::kMaxBoxes;
    for (std::uint32_t i = 0U; i < result->count; ++i) {
        const OdDetection &source = g_postprocess_buffer[i];
        /* Pipe2 keeps the complete 800x480 crop and letterboxes it into the
         * 480x480 model input. Undo the vertical padding before mapping back to
         * the Pipe1 surface. */
        constexpr Float kContentHeight = 288.0F;
        constexpr Float kPadTop = 96.0F;
        const Float display_x_scale =
            static_cast<Float>(memory_allocator::kFrameWidth) /
            static_cast<Float>(kInputSize);
        const Float display_y_scale =
            static_cast<Float>(memory_allocator::kFrameHeight) /
            kContentHeight;
        const Float left = (source.x_center - source.width * 0.5F) *
                           display_x_scale * static_cast<Float>(kInputSize);
        const Float top = ((source.y_center - source.height * 0.5F) *
                               static_cast<Float>(kInputSize) -
                           kPadTop) *
                          display_y_scale;
        result->boxes[i].x = ClampCoordinate(left, memory_allocator::kFrameWidth);
        result->boxes[i].y = ClampCoordinate(top, memory_allocator::kFrameHeight);
        result->boxes[i].width = ClampCoordinate(
            source.width * display_x_scale * static_cast<Float>(kInputSize),
            memory_allocator::kFrameWidth);
        result->boxes[i].height = ClampCoordinate(
            source.height * static_cast<Float>(kInputSize) * display_y_scale,
            memory_allocator::kFrameHeight);
        result->boxes[i].confidence = source.confidence;
    }
    return true;
}

void LogOutputObjectness(const stai_network_info &info, stai_ptr *outputs)
{
    constexpr std::size_t kOutputChannels = 18U;
    constexpr float kConfidenceLogit = 0.4054651081F; // logit(0.6)
    for (std::size_t level = 0U; level < 3U; ++level) {
        const std::size_t slot = g_output_order[level];
        const std::size_t bytes = info.outputs[slot].size_bytes;
        const auto *values = reinterpret_cast<const std::int8_t *>(outputs[slot]);
        std::int32_t minimum = 127;
        std::int32_t maximum = -128;
        std::int32_t maximum_objectness = -128;
        std::size_t maximum_objectness_cell = 0U;
        for (std::size_t i = 0U; i < bytes; ++i) {
            const std::int32_t value = values[i];
            minimum = value < minimum ? value : minimum;
            maximum = value > maximum ? value : maximum;
            if ((i % kOutputChannels) == 4U && value > maximum_objectness) {
                maximum_objectness = value;
                maximum_objectness_cell = i / kOutputChannels;
            }
        }

        const float scale = info.outputs[slot].scale.data[0];
        const std::int32_t zero_point = info.outputs[slot].zeropoint.data[0];
        const float threshold_raw =
            static_cast<float>(zero_point) + kConfidenceLogit / scale;
        AI_INFERENCE_TRACE(reinterpret_cast<const UB *>(
                      "ai: output level=%c slot=%u bytes=%u min=%d max=%d "
                      "obj_raw_max=%d obj_cell=%u threshold_raw_x1000=%u "
                      "scale_x100000=%u zp=%d\n"),
                  level == 0U ? 'S' : (level == 1U ? 'M' : 'L'),
                  static_cast<unsigned int>(slot),
                  static_cast<unsigned int>(bytes),
                  static_cast<int>(minimum), static_cast<int>(maximum),
                  static_cast<int>(maximum_objectness),
                  static_cast<unsigned int>(maximum_objectness_cell),
                  static_cast<unsigned int>(threshold_raw * 1000.0F),
                  static_cast<unsigned int>(scale * 100000.0F),
                  static_cast<int>(zero_point));
    }
}

std::uint32_t ConfidenceMilli(float confidence)
{
    if (confidence <= 0.0F) {
        return 0U;
    }
    return static_cast<std::uint32_t>(confidence * 1000.0F + 0.5F);
}

void LogBoxes(const char *stage, const DetectionSet &boxes)
{
    AI_INFERENCE_TRACE(reinterpret_cast<const UB *>(
                  "ai: boxes stage=%s model=%u capture=%u count=%u\n"),
              stage, static_cast<unsigned int>(boxes.model_sequence),
              static_cast<unsigned int>(boxes.capture_sequence),
              static_cast<unsigned int>(boxes.count));
    const std::uint32_t count = boxes.count < memory_allocator::kMaxBoxes
                                    ? boxes.count
                                    : memory_allocator::kMaxBoxes;
    for (std::uint32_t i = 0U; i < count; ++i) {
        const memory_allocator::Box &box = boxes.boxes[i];
        AI_INFERENCE_TRACE(reinterpret_cast<const UB *>(
                      "ai: box stage=%s index=%u x=%d y=%d w=%d h=%d conf_milli=%u\n"),
                  stage, static_cast<unsigned int>(i),
                  static_cast<int>(box.x), static_cast<int>(box.y),
                  static_cast<int>(box.width), static_cast<int>(box.height),
                  static_cast<unsigned int>(ConfidenceMilli(box.confidence)));
    }
}
#endif

#if defined(AI_MODEL_FACE) || defined(AI_DYNAMIC_MODEL_SWITCHING)
constexpr std::size_t kFaceInputSize = 128U;
constexpr std::size_t kFaceBoxes0 = 512U;
constexpr std::size_t kFaceBoxes1 = 384U;
std::size_t g_face_output_order[4]{};

bool InitializeFacePostprocess(const stai_network_info &info)
{
    if (info.n_outputs != 4U || info.outputs == nullptr) {
        return false;
    }
    /* The generated model currently emits box0, score0, score1, box1.
     * Keep the mapping size-based so regeneration is harmless if the two
     * output branches are reordered by ST Edge AI. */
    bool found_box0 = false;
    bool found_score0 = false;
    bool found_score1 = false;
    bool found_box1 = false;
    for (std::size_t i = 0U; i < info.n_outputs; ++i) {
        switch (info.outputs[i].size_bytes) {
        case kFaceBoxes0 * 16U:
            g_face_output_order[0] = i;
            found_box0 = true;
            break;
        case kFaceBoxes0:
            g_face_output_order[1] = i;
            found_score0 = true;
            break;
        case kFaceBoxes1:
            g_face_output_order[2] = i;
            found_score1 = true;
            break;
        case kFaceBoxes1 * 16U:
            g_face_output_order[3] = i;
            found_box1 = true;
            break;
        default:
            return false;
        }
    }
    if (!found_box0 || !found_score0 || !found_score1 || !found_box1) {
        return false;
    }

    const std::size_t box0 = g_face_output_order[0];
    const std::size_t score0 = g_face_output_order[1];
    const std::size_t score1 = g_face_output_order[2];
    const std::size_t box1 = g_face_output_order[3];
    return ai_face_postprocess_initialize(
               info.outputs[box0].scale.data[0],
               info.outputs[box0].zeropoint.data[0],
               info.outputs[score0].scale.data[0],
               info.outputs[score0].zeropoint.data[0],
               info.outputs[box1].scale.data[0],
               info.outputs[box1].zeropoint.data[0],
               info.outputs[score1].scale.data[0],
               info.outputs[score1].zeropoint.data[0]) == 0;
}

std::int16_t ClampFaceCoordinate(Float value, std::int32_t limit)
{
    if (value <= 0.0F) {
        return 0;
    }
    if (value >= static_cast<Float>(limit)) {
        return static_cast<std::int16_t>(limit);
    }
    return static_cast<std::int16_t>(value);
}

bool ConvertFaceDetections(stai_ptr *outputs, bool from_pipe2,
                          DetectionSet *result)
{
    ai_face_detection_t detections[memory_allocator::kMaxBoxes]{};
    std::uint32_t count = 0U;
    if (ai_face_postprocess_run(
            outputs[g_face_output_order[0]], outputs[g_face_output_order[1]],
            outputs[g_face_output_order[3]], outputs[g_face_output_order[2]],
            detections, memory_allocator::kMaxBoxes, &count) != 0) {
        return false;
    }

    result->count = count;
    for (std::uint32_t i = 0U; i < result->count; ++i) {
        const ai_face_detection_t &source = detections[i];
        Float left = 0.0F;
        Float top = 0.0F;
        Float box_width = 0.0F;
        Float box_height = 0.0F;
        if (from_pipe2) {
            /* Pipe2 contains the complete Pipe1 crop letterboxed into the
             * 128x128 model input. */
            constexpr Float kContentHeight = 77.0F;
            constexpr Float kPadTop = 25.0F;
            constexpr Float kInputSize = 128.0F;
            left = (source.x_center - source.width * 0.5F) *
                   static_cast<Float>(memory_allocator::kFrameWidth);
            top = ((source.y_center - source.height * 0.5F) * kInputSize -
                   kPadTop) *
                  static_cast<Float>(memory_allocator::kFrameHeight) /
                  kContentHeight;
            box_width = source.width * kInputSize *
                        static_cast<Float>(memory_allocator::kFrameWidth) /
                        kInputSize;
            box_height = source.height * kInputSize *
                         static_cast<Float>(memory_allocator::kFrameHeight) /
                         kContentHeight;
        } else {
            /* CPU fallback: face input is the centered square crop of the
             * 800x480 Pipe1 surface. */
            constexpr Float kFaceCropSize =
                static_cast<Float>(memory_allocator::kFrameHeight);
            constexpr Float kFaceCropX =
                static_cast<Float>(memory_allocator::kFrameWidth -
                                   memory_allocator::kFrameHeight) /
                2.0F;
            left = kFaceCropX +
                   (source.x_center - source.width * 0.5F) * kFaceCropSize;
            top = (source.y_center - source.height * 0.5F) * kFaceCropSize;
            box_width = source.width * kFaceCropSize;
            box_height = source.height * kFaceCropSize;
        }
        result->boxes[i].x = ClampFaceCoordinate(
            left, memory_allocator::kFrameWidth);
        result->boxes[i].y = ClampFaceCoordinate(
            top, memory_allocator::kFrameHeight);
        result->boxes[i].width = ClampFaceCoordinate(
            box_width,
            memory_allocator::kFrameWidth);
        result->boxes[i].height = ClampFaceCoordinate(
            box_height,
            memory_allocator::kFrameHeight);
        result->boxes[i].confidence = source.confidence;
    }
    return true;
}

#if AI_INFERENCE_DIAGNOSTICS
void LogFaceOutputs(const stai_network_info &info, stai_ptr *outputs)
{
    constexpr float kConfidenceLogit = -0.6190392084F; // logit(0.35)
    for (std::size_t i = 0U; i < info.n_outputs; ++i) {
        if (info.outputs[i].size_bytes != kFaceBoxes0 &&
            info.outputs[i].size_bytes != kFaceBoxes1) {
            continue;
        }
        const auto *values = reinterpret_cast<const std::int8_t *>(outputs[i]);
        const std::size_t count = info.outputs[i].size_bytes;
        std::int32_t minimum = 127;
        std::int32_t maximum = -128;
        std::size_t maximum_index = 0U;
        for (std::size_t j = 0U; j < count; ++j) {
            const std::int32_t value = values[j];
            minimum = value < minimum ? value : minimum;
            if (value > maximum) {
                maximum = value;
                maximum_index = j;
            }
        }
        const float scale = info.outputs[i].scale.data[0];
        const std::int32_t zero_point = info.outputs[i].zeropoint.data[0];
        const float threshold = static_cast<float>(zero_point) +
                                kConfidenceLogit / scale;
        tm_printf(reinterpret_cast<const UB *>(
                      "ai: face score slot=%u boxes=%u min=%d max=%d "
                  "max_index=%u threshold_x1000=%u scale_x100000=%u "
                  "zp=%d max_prob_x1000=%u\n"),
                  static_cast<unsigned int>(i),
                  static_cast<unsigned int>(count),
                  static_cast<int>(minimum), static_cast<int>(maximum),
                  static_cast<unsigned int>(maximum_index),
                  static_cast<unsigned int>(threshold * 1000.0F),
                  static_cast<unsigned int>(scale * 100000.0F),
                  static_cast<int>(zero_point),
                  static_cast<unsigned int>(
                      (1.0F / (1.0F + __builtin_expf(
                                        -((static_cast<float>(maximum) -
                                           static_cast<float>(zero_point)) *
                                          scale)))) *
                      1000.0F));
    }
}
#endif
#endif

#if defined(AI_MODEL_SEGMENTATION) || defined(AI_DYNAMIC_MODEL_SWITCHING)
constexpr std::size_t kSegmentationInputWidth = 320U;
constexpr std::size_t kSegmentationInputHeight = 320U;
constexpr std::size_t kSegmentationMaskWidth = 320U;
constexpr std::size_t kSegmentationMaskHeight = 320U;
constexpr std::size_t kSegmentationMaskBytes =
    kSegmentationMaskWidth * kSegmentationMaskHeight;
constexpr std::uintptr_t kSegmentationMaskBuffers[2] = {
    0x91A00000UL,
    0x91A20000UL,
};

bool ConvertSegmentationMask(stai_ptr output, std::uint8_t mask_index,
                             SegmentationSet *result)
{
    if (output == nullptr || result == nullptr || mask_index > 1U) {
        return false;
    }
    const auto *logits = reinterpret_cast<const std::int8_t *>(output);
    auto *mask = reinterpret_cast<std::uint8_t *>(
        kSegmentationMaskBuffers[mask_index]);
    std::uint32_t foreground_pixels = 0U;
    for (std::size_t i = 0U; i < kSegmentationMaskBytes; ++i) {
        mask[i] = logits[2U * i + 1U] > logits[2U * i] ? 1U : 0U;
        foreground_pixels += mask[i];
    }
    result->mask_address = kSegmentationMaskBuffers[mask_index];
    result->mask_width = static_cast<std::uint16_t>(kSegmentationMaskWidth);
    result->mask_height = static_cast<std::uint16_t>(kSegmentationMaskHeight);
    result->mask_foreground_pixels = foreground_pixels;
    return true;
}
#endif

} // namespace

Error ModelManager::Initialize(memory_allocator::MemoryAllocator &memory,
                               cache::CacheDriver &cache)
{
    if (initialized_) {
        return {ErrorCode::kAlreadyInitialized, 0U, "ai.initialize"};
    }
    memory_ = &memory;
    cache_ = &cache;
#if defined(AI_DYNAMIC_MODEL_SWITCHING)
    if (active_model_ == nullptr) {
        switch (model_kind_) {
        case ModelKind::kSegmentation:
            active_model_ = &segmentation_model_;
            break;
        case ModelKind::kFace:
            active_model_ = &face_model_;
            break;
        case ModelKind::kPerson:
        default:
            active_model_ = &person_model_;
            break;
        }
    }
    npu::Status npu_status = npu_.Initialize(*active_model_);
    if (npu_status.Ok()) {
        /* Keep every generated network context initialized.  Its generated
         * EC command blob is copied from external flash to the model's
         * runtime buffer here, once, instead of during every switch. */
        const model_manager::ModelKind preload_order[] = {
            ModelKind::kPerson, ModelKind::kSegmentation, ModelKind::kFace};
        for (const ModelKind kind : preload_order) {
            model_manager::Model *candidate = nullptr;
            switch (kind) {
            case ModelKind::kPerson:
                candidate = &person_model_;
                break;
            case ModelKind::kSegmentation:
                candidate = &segmentation_model_;
                break;
            case ModelKind::kFace:
                candidate = &face_model_;
                break;
            }
            if (candidate == active_model_) {
                continue;
            }
            const bool was_loaded = npu_.IsLoaded(*candidate);
            npu_status = npu_.Preload(*candidate);
            if (!npu_status.Ok()) {
                break;
            }
            if (!was_loaded) {
                tm_printf(reinterpret_cast<const UB *>(
                              "ai: model preloaded=%s\n"),
                          reinterpret_cast<const UB *>(Describe(kind).name));
            }
        }
    }
#else
    npu::Status npu_status = npu_.Initialize(model_);
#endif
    last_npu_status_ = npu_status;
    if (!npu_status.Ok()) {
        last_error_ = npu_status.error.detail;
        return npu_status.error;
    }

    npu_status = npu_.GetInfo(&info_);
    last_npu_status_ = npu_status;
    last_error_ = npu_status.error.detail;
    const std::uint16_t expected_outputs =
#if defined(AI_DYNAMIC_MODEL_SWITCHING)
        Describe(model_kind_).output_count;
#else
#if defined(AI_MODEL_SEGMENTATION)
        1U;
#elif defined(AI_MODEL_FACE)
        4U;
#else
        3U;
#endif
#endif
    if (!npu_status.Ok() || info_.n_inputs != 1U ||
        info_.n_outputs != expected_outputs ||
        info_.inputs == nullptr || info_.outputs == nullptr) {
        return {ErrorCode::kModel, last_error_, "ai.model_info"};
    }

    stai_size output_count = 0U;
    npu_status = npu_.GetOutputs(outputs_, &output_count);
    last_npu_status_ = npu_status;
    if (!npu_status.Ok() || output_count != expected_outputs) {
        last_error_ = npu_status.error.detail;
        return {ErrorCode::kModel, last_error_, "ai.model_outputs"};
    }

    /* Neural-ART uses STAI_FLAG_OVERRIDE for both compiler-owned and
     * user-owned outputs. The reliable indication of --no-outputs-allocation
     * is the pointer returned before the application supplies buffers: a
     * user-owned output is still NULL at this point. Do not infer ownership
     * from STAI flags, otherwise allocator-provided buffers are never bound
     * and the NPU receives NULL output addresses. */
    dynamic_outputs_ = false;
    for (std::uint16_t i = 0U; i < info_.n_outputs; ++i) {
        const bool output_is_user_owned = outputs_[i] == nullptr;
        if (i == 0U) {
            dynamic_outputs_ = output_is_user_owned;
        } else if (dynamic_outputs_ != output_is_user_owned) {
            return {ErrorCode::kModel, i, "ai.mixed_output_ownership"};
        }
    }
#if defined(AI_DYNAMIC_MODEL_SWITCHING)
    const model_manager::ModelDescriptor &descriptor = Describe(model_kind_);
    if (info_.inputs[0].size_bytes !=
            static_cast<std::size_t>(descriptor.input_width) *
                descriptor.input_height * 3U) {
        return {ErrorCode::kModel, 0U, "ai.segmentation_tensor_shape"};
    }
#endif
#if defined(AI_MODEL_SEGMENTATION)
    if (info_.inputs[0].size_bytes !=
            kSegmentationInputWidth * kSegmentationInputHeight * 3U ||
        info_.outputs[0].size_bytes != kSegmentationMaskBytes * 2U) {
        return {ErrorCode::kModel, 0U, "ai.segmentation_tensor_shape"};
    }
#elif defined(AI_MODEL_FACE)
    if (info_.inputs[0].size_bytes != 128U * 128U * 3U ||
        info_.outputs[0].size_bytes != 8192U ||
        info_.outputs[1].size_bytes != 512U ||
        info_.outputs[2].size_bytes != 384U ||
        info_.outputs[3].size_bytes != 6144U) {
        return {ErrorCode::kModel, 0U, "ai.face_tensor_shape"};
    }
#endif
#if defined(AI_DYNAMIC_MODEL_SWITCHING)
    bool postprocess_ok = false;
    switch (model_kind_) {
    case ModelKind::kPerson:
        postprocess_ok = InitializePostprocess(info_);
        break;
    case ModelKind::kFace:
        postprocess_ok = InitializeFacePostprocess(info_);
        break;
    case ModelKind::kSegmentation:
        postprocess_ok = info_.n_outputs == 1U &&
                         info_.outputs[0].size_bytes ==
                             kSegmentationMaskBytes * 2U;
        break;
    }
    if (!postprocess_ok) {
        return {ErrorCode::kModel, 0U, "ai.postprocess_initialize"};
    }
#elif defined(AI_MODEL_PERSON)
    if (!InitializePostprocess(info_)) {
        return {ErrorCode::kModel, 0U, "ai.postprocess_initialize"};
    }
#elif defined(AI_MODEL_FACE)
    if (!InitializeFacePostprocess(info_)) {
        return {ErrorCode::kModel, 0U, "ai.face_postprocess_initialize"};
    }
#endif

    initialized_ = true;
    last_error_ = 0U;
    return {ErrorCode::kOk, 0U, "ai.initialize"};
}

Error ModelManager::SwitchModel(ModelKind kind)
{
#if !defined(AI_DYNAMIC_MODEL_SWITCHING)
    (void)kind;
    return {ErrorCode::kInvalidState, 0U, "ai.switch_model.disabled"};
#else
    if (!initialized_ || memory_ == nullptr || cache_ == nullptr) {
        return {ErrorCode::kNotInitialized, 0U, "ai.switch_model"};
    }
    if (kind == model_kind_) {
        return {ErrorCode::kOk, 0U, "ai.switch_model"};
    }

    initialized_ = false;
    model_kind_ = kind;
    active_model_ = nullptr;
    const Error status = Initialize(*memory_, *cache_);
    if (!status.Ok()) {
        return status;
    }
    tm_printf(reinterpret_cast<const UB *>(
                  "ai: model switched to %s\n"),
              reinterpret_cast<const UB *>(Describe(model_kind_).name));
    return status;
#endif
}

Error ModelManager::RunNetwork()
{
    const npu::Status status = npu_.Run();
    last_npu_status_ = status;
    last_error_ = status.error.detail;
    return status.error;
}

Error ModelManager::TryInfer(
    const memory_allocator::InferenceFrame &frame,
    memory_allocator::BoxSet *result)
{
    if (!initialized_ || memory_ == nullptr) {
        return {ErrorCode::kNotInitialized, 0U, "ai.infer"};
    }
    if (!frame || result == nullptr) {
        return {ErrorCode::kInvalidArgument, 0U, "ai.infer"};
    }

    result->person = {};
    result->face = {};
    result->segmentation = {};
    result->person_valid = false;
    result->face_valid = false;
    result->segmentation_valid = false;
    result->capture_sequence = frame.capture_sequence;
    result->model_sequence = ++model_sequence_;
#if AI_INFERENCE_FPS_DIAGNOSTICS
    const std::uint32_t diagnostic_start = DiagnosticNow();
    const unsigned int irq_start = g_aton_irq_count;
    std::uint32_t input_ms = 0U;
    std::uint32_t npu_ms = 0U;
    std::uint32_t output_cache_ms = 0U;
    std::uint32_t postprocess_ms = 0U;
    std::uint32_t reset_ms = 0U;
#endif
    AI_INFERENCE_TRACE(reinterpret_cast<const UB *>(
                  "ai: input direct begin sequence=%u source=%x size=%u pipe2=%u\n"),
              static_cast<unsigned int>(frame.capture_sequence),
              static_cast<unsigned int>(frame.buffer.address),
              static_cast<unsigned int>(info_.inputs[0].size_bytes),
              static_cast<unsigned int>(frame.from_pipe2));
    if (frame.buffer.size < info_.inputs[0].size_bytes) {
        tm_printf(reinterpret_cast<const UB *>(
                      "ai: input rejected buffer=%u required=%u\n"),
                  static_cast<unsigned int>(frame.buffer.size),
                  static_cast<unsigned int>(info_.inputs[0].size_bytes));
        return {ErrorCode::kInvalidArgument,
                static_cast<std::uint32_t>(frame.buffer.size),
                "ai.direct_input"};
    }
#if defined(AI_DYNAMIC_MODEL_SWITCHING)
    if (Describe(model_kind_).input_from_pipe2 && !frame.from_pipe2) {
        tm_printf(reinterpret_cast<const UB *>(
                      "ai: input rejected source_not_pipe2 model=%s\n"),
                  reinterpret_cast<const UB *>(Describe(model_kind_).name));
        return {ErrorCode::kInvalidArgument, 2U, "ai.direct_input"};
    }
#elif !defined(AI_MODEL_FACE)
    if (!frame.from_pipe2) {
        return {ErrorCode::kInvalidArgument, 2U, "ai.direct_input"};
    }
#endif
    const memory_allocator::Buffer input_buffer{
        frame.buffer.address, info_.inputs[0].size_bytes, frame.buffer.index,
        memory_allocator::Region::kInference};
    AI_INFERENCE_TRACE(reinterpret_cast<const UB *>(
                  "ai: input cache begin sequence=%u size=%u\n"),
              static_cast<unsigned int>(frame.capture_sequence),
              static_cast<unsigned int>(input_buffer.size));
    /* Pipe2 wrote its input through DMA. For a dynamically selected smaller
     * model the inference task has rebuilt the tensor in-place from the same
     * frame, so clean that CPU-written input instead. */
    Error status = frame.input_prepared_by_cpu
                       ? cache_->PrepareForPeripheralRead(input_buffer)
                       : frame.from_pipe2
                             ? cache_->PrepareForCpuRead(input_buffer)
                             : cache_->PrepareForPeripheralRead(input_buffer);
    if (!status.Ok()) {
        return status;
    }
    AI_INFERENCE_TRACE(reinterpret_cast<const UB *>(
              "ai: input cache end sequence=%u\n"),
              static_cast<unsigned int>(frame.capture_sequence));

#if defined(AI_DYNAMIC_MODEL_SWITCHING)
    const stai_return_code set_input = active_model_->SetInput(
#else
    const stai_return_code set_input = model_.SetInput(
#endif
        reinterpret_cast<stai_ptr>(frame.buffer.address),
        info_.inputs[0].size_bytes);
    if (set_input != STAI_SUCCESS) {
        return {ErrorCode::kModel, static_cast<std::uint32_t>(set_input),
                "ai.set_user_input"};
    }
    AI_INFERENCE_TRACE(reinterpret_cast<const UB *>(
                  "ai: input allocator end sequence=%u source=%x\n"),
              static_cast<unsigned int>(frame.capture_sequence),
              static_cast<unsigned int>(frame.buffer.address));

    if (dynamic_outputs_) {
        /* The dynamic allocator exposes the maximum four output slots for
         * every model. A model may consume only a prefix of those slots
         * (person=3, segmentation=1), so reject only insufficient capacity. */
        if (frame.output_count < info_.n_outputs) {
            return {ErrorCode::kInvalidArgument, frame.output_count,
                    "ai.dynamic_output_count"};
        }
        stai_ptr dynamic_output_ptrs[memory_allocator::kMaxModelOutputs]{};
        for (std::uint16_t i = 0U; i < info_.n_outputs; ++i) {
            const memory_allocator::Buffer &output = frame.outputs[i];
            if (!output || output.size < info_.outputs[i].size_bytes ||
                output.alignment == 0U ||
                (output.address % output.alignment) != 0U) {
                return {ErrorCode::kInvalidArgument,
                        static_cast<std::uint32_t>(i),
                        "ai.dynamic_output_buffer"};
            }
            dynamic_output_ptrs[i] =
                reinterpret_cast<stai_ptr>(output.address);
        }
        const npu::Status set_outputs =
            npu_.SetOutputs(dynamic_output_ptrs, info_.n_outputs);
        last_npu_status_ = set_outputs;
        last_error_ = set_outputs.error.detail;
        if (!set_outputs.Ok()) {
            return set_outputs.error;
        }
        for (std::uint16_t i = 0U; i < info_.n_outputs; ++i) {
            outputs_[i] = dynamic_output_ptrs[i];
        }
    }

#if AI_INFERENCE_FPS_DIAGNOSTICS
    input_ms = DiagnosticNow() - diagnostic_start;
#endif

    AI_INFERENCE_TRACE(reinterpret_cast<const UB *>(
                  "ai: npu run call sequence=%u\n"),
              static_cast<unsigned int>(frame.capture_sequence));
#if AI_INFERENCE_FPS_DIAGNOSTICS
    const std::uint32_t npu_start = DiagnosticNow();
#endif
    status = RunNetwork();
#if AI_INFERENCE_FPS_DIAGNOSTICS
    npu_ms = DiagnosticNow() - npu_start;
#endif
    AI_INFERENCE_TRACE(reinterpret_cast<const UB *>(
                  "ai: npu run return sequence=%u code=%u detail=%x\n"),
              static_cast<unsigned int>(frame.capture_sequence),
              static_cast<unsigned int>(status.code),
              static_cast<unsigned int>(status.detail));
    if (!status.Ok()) {
        return status;
    }
    for (std::uint16_t i = 0U; i < info_.n_outputs; ++i) {
        const memory_allocator::Buffer output_buffer =
            dynamic_outputs_
                ? frame.outputs[i]
                : memory_allocator::Buffer{
                      reinterpret_cast<std::uintptr_t>(outputs_[i]),
                      info_.outputs[i].size_bytes, 0U,
                      memory_allocator::Region::kInference};
        status = cache_->PrepareForCpuRead(output_buffer);
        if (!status.Ok()) {
            return status;
        }
    }
#if AI_INFERENCE_FPS_DIAGNOSTICS
    const std::uint32_t postprocess_start = DiagnosticNow();
    output_cache_ms = postprocess_start - npu_start - npu_ms;
#endif
#if defined(AI_DYNAMIC_MODEL_SWITCHING)
    switch (model_kind_) {
    case ModelKind::kSegmentation:
        if (!ConvertSegmentationMask(outputs_[0], mask_buffer_index_,
                                     &result->segmentation)) {
            return {ErrorCode::kModel, 0U, "ai.segmentation_postprocess"};
        }
        result->segmentation_valid = true;
        mask_buffer_index_ ^= 1U;
        break;
    case ModelKind::kFace:
#if AI_INFERENCE_DIAGNOSTICS
        LogFaceOutputs(info_, outputs_);
#endif
        if (!ConvertFaceDetections(outputs_, true, &result->face)) {
            return {ErrorCode::kModel, 0U, "ai.face_postprocess"};
        }
        result->face_valid = true;
#if AI_INFERENCE_DIAGNOSTICS
        LogBoxes("postprocess-face", result->face);
#endif
        break;
    case ModelKind::kPerson:
#if AI_INFERENCE_DIAGNOSTICS
        LogOutputObjectness(info_, outputs_);
#endif
        if (!ConvertDetections(outputs_, &result->person)) {
            return {ErrorCode::kModel, 0U, "ai.postprocess"};
        }
        result->person_valid = true;
#if AI_INFERENCE_DIAGNOSTICS
        LogBoxes("postprocess-person", result->person);
#endif
        break;
    }
#elif defined(AI_MODEL_SEGMENTATION)
    if (!ConvertSegmentationMask(outputs_[0], mask_buffer_index_,
                                 &result->segmentation)) {
        return {ErrorCode::kModel, 0U, "ai.segmentation_postprocess"};
    }
    result->segmentation_valid = true;
    mask_buffer_index_ ^= 1U;
#elif defined(AI_MODEL_FACE)
#if AI_INFERENCE_DIAGNOSTICS
    LogFaceOutputs(info_, outputs_);
#endif
    if (!ConvertFaceDetections(outputs_, frame.from_pipe2, &result->face)) {
        return {ErrorCode::kModel, 0U, "ai.face_postprocess"};
    }
    result->face_valid = true;
#else
#if AI_INFERENCE_DIAGNOSTICS
    LogOutputObjectness(info_, outputs_);
#endif
    if (!ConvertDetections(outputs_, &result->person)) {
        return {ErrorCode::kModel, 0U, "ai.postprocess"};
    }
    result->person_valid = true;
#if AI_INFERENCE_DIAGNOSTICS
    LogBoxes("postprocess-person", result->person);
#endif
#endif

#if AI_INFERENCE_FPS_DIAGNOSTICS
    postprocess_ms = DiagnosticNow() - postprocess_start;
    const std::uint32_t reset_start = DiagnosticNow();
#endif

    const npu::Status npu_status = npu_.NewInference();
    last_npu_status_ = npu_status;
    last_error_ = npu_status.error.detail;
    if (!npu_status.Ok()) {
        return npu_status.error;
    }
#if AI_INFERENCE_FPS_DIAGNOSTICS
    reset_ms = DiagnosticNow() - reset_start;
    if ((result->model_sequence % 10U) == 0U) {
        UB diagnostic_line[192] = {};
        (void)tm_sprintf(
            diagnostic_line,
            reinterpret_cast<const UB *>(
                "ai: stages input_ms=%u npu_ms=%u output_ms=%u post_ms=%u "
                "reset_ms=%u total_ms=%u irq_delta=%u irq_last=%x count=%u "
                "mask_px=%u\n"),
            static_cast<unsigned int>(input_ms),
            static_cast<unsigned int>(npu_ms),
            static_cast<unsigned int>(output_cache_ms),
            static_cast<unsigned int>(postprocess_ms),
            static_cast<unsigned int>(reset_ms),
            static_cast<unsigned int>(DiagnosticNow() - diagnostic_start),
            static_cast<unsigned int>(g_aton_irq_count - irq_start),
            g_aton_last_irqs,
            static_cast<unsigned int>(result->person.count + result->face.count),
            static_cast<unsigned int>(result->segmentation.mask_foreground_pixels));
        tm_putstring(diagnostic_line);
    }
#endif
    const std::uint32_t result_count =
        result->person.count + result->face.count;
    return {ErrorCode::kOk, result_count, "ai.infer"};
}

Error ModelManager::Shutdown()
{
    if (!initialized_) {
        return {ErrorCode::kNotInitialized, 0U, "ai.shutdown"};
    }
    const npu::Status status = npu_.Shutdown();
    last_npu_status_ = status;
    last_error_ = status.error.detail;
    initialized_ = false;
    return status.error;
}

} // namespace uai::ai
