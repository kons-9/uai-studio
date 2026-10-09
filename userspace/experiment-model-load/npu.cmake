set(_model_dir "${CMAKE_CURRENT_SOURCE_DIR}/models/generated")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_model_dir}/contract.json")
foreach(_file network.c network_ecblobs.h stai_network.c stai_network.h weights.bin contract.json)
    if(NOT EXISTS "${_model_dir}/${_file}")
        message(FATAL_ERROR "Missing local model: ${_model_dir}/${_file}. Run model-generate first.")
    endif()
endforeach()
if(NOT STEDGEAI_LIB_DIR)
    message(FATAL_ERROR "EXPERIMENT_MODEL_NPU requires STEDGEAI_LIB_DIR (vendor Inc/, Npu/, Lib/).")
endif()
execute_process(COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tool/model.py" contract "${_model_dir}/contract.json"
    RESULT_VARIABLE _contract_result OUTPUT_VARIABLE _contract ERROR_VARIABLE _contract_error OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _contract_result EQUAL 0)
    message(FATAL_ERROR "Invalid model contract: ${_contract_error}")
endif()
list(GET _contract 0 _runtime_version)
list(GET _contract 2 _input_bytes)
list(GET _contract 3 _output_bytes)
set(_runtime_library "${STEDGEAI_LIB_DIR}/Lib/GCC/ARMCortexM55/NetworkRuntime${_runtime_version}_CM55_GCC.a")
if(NOT EXISTS "${_runtime_library}")
    message(FATAL_ERROR "Model runtime archive is missing: ${_runtime_library}")
endif()

set(_npu_includes "${_model_dir}" "${STEDGEAI_LIB_DIR}/Inc"
    "${STEDGEAI_LIB_DIR}/Npu/ll_aton" "${STEDGEAI_LIB_DIR}/Npu/Devices/STM32N6xx"
    "${CUBE}/Drivers/CMSIS/DSP/Include")
set(_npu_definitions EXPERIMENT_MODEL_NPU=1 LL_ATON_PLATFORM=LL_ATON_PLAT_STM32N6
    LL_ATON_OSAL=LL_ATON_OSAL_BARE_METAL LL_ATON_RT_MODE=LL_ATON_RT_ASYNC
    LL_ATON_SW_FALLBACK LL_ATON_DBG_BUFFER_INFO_EXCLUDED=1 NPU_CACHE_EXTERNAL_HAL_MSP
    __int64_t_defined=1 EXPERIMENT_MODEL_INPUT_BYTES=${_input_bytes} EXPERIMENT_MODEL_OUTPUT_BYTES=${_output_bytes})
target_include_directories(${TARGET_NAME} PRIVATE ${_npu_includes})
target_compile_definitions(${TARGET_NAME} PRIVATE ${_npu_definitions})
target_compile_options(${TARGET_NAME} PRIVATE -O3 -mcmse)
get_target_property(_board_includes ${TARGET_NAME} INCLUDE_DIRECTORIES)
get_target_property(_board_definitions ${TARGET_NAME} COMPILE_DEFINITIONS)
add_library(experiment_model_generated OBJECT "${CMAKE_CURRENT_SOURCE_DIR}/src/npu_model.c")
target_include_directories(experiment_model_generated PRIVATE ${_board_includes})
target_compile_definitions(experiment_model_generated PRIVATE ${_board_definitions})
target_compile_options(experiment_model_generated PRIVATE -O3 -mcmse)
target_sources(${TARGET_NAME} PRIVATE $<TARGET_OBJECTS:experiment_model_generated>)
foreach(_source ecloader ll_aton ll_aton_cipher ll_aton_dbgtrc ll_aton_lib
        ll_aton_lib_sw_operators ll_aton_runtime ll_aton_stai_internal ll_aton_util ll_sw_float ll_sw_integer)
    target_sources(${TARGET_NAME} PRIVATE "${STEDGEAI_LIB_DIR}/Npu/ll_aton/${_source}.c")
endforeach()
set(_npu_cache "${STEDGEAI_LIB_DIR}/Npu/Devices/STM32N6xx/npu_cache.c")
set_source_files_properties("${_npu_cache}" PROPERTIES COMPILE_DEFINITIONS
    "HAL_CACHEAXI_MspInit=model_load_HAL_CACHEAXI_MspInit;HAL_CACHEAXI_MspDeInit=model_load_HAL_CACHEAXI_MspDeInit")
target_sources(${TARGET_NAME} PRIVATE "${_npu_cache}" "${STEDGEAI_LIB_DIR}/Npu/Devices/STM32N6xx/mcu_cache.c")
get_target_property(_board_sources stm32n6570_dk SOURCES)
list(FILTER _board_sources INCLUDE REGEX "/stm32n6xx_hal_ramcfg\\.c$")
if(NOT _board_sources)
    target_sources(${TARGET_NAME} PRIVATE "${HAL}/Src/stm32n6xx_hal_ramcfg.c")
endif()
target_link_libraries(${TARGET_NAME} PRIVATE "${_runtime_library}")
target_link_options(${TARGET_NAME} PRIVATE -Wl,--undefined=NPU0_IRQHandler)

set(_package "${CMAKE_CURRENT_BINARY_DIR}/model-package")
add_custom_command(OUTPUT "${_package}/model_expected.hpp" "${_package}/manifest.bin"
        "${_package}/weights.bin" "${_package}/blob.bin"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tool/model.py" package
        --model-dir "${_model_dir}" --object $<TARGET_OBJECTS:experiment_model_generated>
        --objcopy "${CMAKE_OBJCOPY}" --objdump "${CMAKE_OBJDUMP}" --output-dir "${_package}"
    DEPENDS experiment_model_generated $<TARGET_OBJECTS:experiment_model_generated>
        "${_model_dir}/weights.bin" "${_model_dir}/contract.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/tool/model.py" "${CMAKE_CURRENT_SOURCE_DIR}/tool/manifest.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tool/expected.py"
    COMMAND_EXPAND_LISTS VERBATIM)
add_custom_target(experiment_model_package DEPENDS "${_package}/model_expected.hpp")
add_dependencies(${TARGET_NAME} experiment_model_package)
target_include_directories(${TARGET_NAME} BEFORE PRIVATE "${_package}")