if(MODEL_LOADER_MULTI)
    if(NOT DEFINED MODEL_LOADER_MODELS)
        set(MODEL_LOADER_MODELS "person;face;seg" CACHE STRING "App-local model registry names")
    endif()
    set(_model_names ${MODEL_LOADER_MODELS})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tool/registry.py")
    execute_process(COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tool/registry.py"
        --models ${_model_names} --header "${CMAKE_CURRENT_BINARY_DIR}/model_registry.hpp"
        --linker "${CMAKE_CURRENT_BINARY_DIR}/model-overlays.ld"
        RESULT_VARIABLE _registry_result ERROR_VARIABLE _registry_error)
    if(NOT _registry_result EQUAL 0)
        message(FATAL_ERROR "Invalid model registry: ${_registry_error}")
    endif()
    target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_definitions(${TARGET_NAME} PRIVATE MODEL_LOADER_MULTI=1)
    target_sources(${TARGET_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/npu_model.c" "${CMAKE_CURRENT_SOURCE_DIR}/src/npu_multi.cpp")
    target_link_options(${TARGET_NAME} BEFORE PRIVATE "-T${CMAKE_CURRENT_BINARY_DIR}/model-overlays.ld")
    set_property(TARGET ${TARGET_NAME} APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/model-overlays.ld")
else()
    set(_model_names generated)
endif()
if(NOT STEDGEAI_LIB_DIR)
    message(FATAL_ERROR "MODEL_LOADER_NPU requires STEDGEAI_LIB_DIR (vendor Inc/, Npu/, Lib/).")
endif()
set(_npu_includes "${STEDGEAI_LIB_DIR}/Inc"
    "${STEDGEAI_LIB_DIR}/Npu/ll_aton" "${STEDGEAI_LIB_DIR}/Npu/Devices/STM32N6xx"
    "${CUBE}/Drivers/CMSIS/DSP/Include")
set(_npu_definitions MODEL_LOADER_NPU=1 LL_ATON_PLATFORM=LL_ATON_PLAT_STM32N6
    LL_ATON_OSAL=LL_ATON_OSAL_BARE_METAL LL_ATON_RT_MODE=LL_ATON_RT_ASYNC
    LL_ATON_SW_FALLBACK LL_ATON_DBG_BUFFER_INFO_EXCLUDED=1 NPU_CACHE_EXTERNAL_HAL_MSP
    __int64_t_defined=1)
target_include_directories(${TARGET_NAME} PRIVATE ${_npu_includes})
target_compile_definitions(${TARGET_NAME} PRIVATE ${_npu_definitions})
target_compile_options(${TARGET_NAME} PRIVATE -O3 -mcmse)
set(_package_root "${CMAKE_CURRENT_BINARY_DIR}/model-package")
set(_package_dirs)
set(_package_headers)
foreach(_name IN LISTS _model_names)
    set(_model_dir "${CMAKE_CURRENT_SOURCE_DIR}/models/${_name}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_model_dir}/contract.json")
    set(_semantic_dependencies)
    if(EXISTS "${_model_dir}/semantics.json")
        list(APPEND _semantic_dependencies "${_model_dir}/semantics.json")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_model_dir}/semantics.json")
    foreach(_file network.c network_ecblobs.h stai_network.c stai_network.h weights.bin contract.json)
        if(NOT EXISTS "${_model_dir}/${_file}")
            message(FATAL_ERROR "Missing local model: ${_model_dir}/${_file}. Run model-generate MODEL_NAME=${_name} first.")
        endif()
    endforeach()
    execute_process(COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tool/model.py" contract "${_model_dir}/contract.json"
        RESULT_VARIABLE _contract_result OUTPUT_VARIABLE _contract ERROR_VARIABLE _contract_error OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _contract_result EQUAL 0)
        message(FATAL_ERROR "Invalid model contract: ${_contract_error}")
    endif()
    list(GET _contract 0 _version)
    list(GET _contract 2 _input_bytes)
    list(GET _contract 3 _output_bytes)
    if(DEFINED _runtime_version AND NOT _runtime_version STREQUAL _version)
        message(FATAL_ERROR "All models must use the same ST runtime version")
    endif()
    set(_runtime_version "${_version}")
    if(MODEL_LOADER_MULTI)
        execute_process(COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tool/model.py" contract "${_model_dir}/contract.json" --tensors
            RESULT_VARIABLE _tensor_result OUTPUT_VARIABLE _tensors OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(NOT _tensor_result EQUAL 0 OR NOT EXISTS "${_model_dir}/model_symbols.h")
            message(FATAL_ERROR "${_name} needs v2 descriptors and model_symbols.h; regenerate the named model")
        endif()
        list(GET _tensors 1 _output_count)
        set(_source "${CMAKE_CURRENT_SOURCE_DIR}/src/network_api.c")
        set(_section ".model_blob_${_name}")
        set(_model_definitions MODEL_PREFIX=${_name} "MODEL_BLOB_SECTION=\"${_section}\""
            MODEL_INPUT_BYTES=${_input_bytes} MODEL_OUTPUT_COUNT=${_output_count})
        foreach(_index RANGE 1 ${_output_count})
            math(EXPR _position "${_index} + 1")
            list(GET _tensors ${_position} _bytes)
            list(APPEND _model_definitions MODEL_OUTPUT_${_index}_BYTES=${_bytes})
        endforeach()
        set(_package "${_package_root}/${_name}")
    else()
        set(_source "${CMAKE_CURRENT_SOURCE_DIR}/src/npu_model.c")
        set(_section ".model_command_blob")
        set(_model_definitions MODEL_LOADER_INPUT_BYTES=${_input_bytes} MODEL_LOADER_OUTPUT_BYTES=${_output_bytes})
        set(_package "${_package_root}")
    endif()
    get_target_property(_board_includes ${TARGET_NAME} INCLUDE_DIRECTORIES)
    get_target_property(_board_definitions ${TARGET_NAME} COMPILE_DEFINITIONS)
    add_library(model_loader_model_${_name} OBJECT "${_source}")
    target_include_directories(model_loader_model_${_name} PRIVATE "${_model_dir}" ${_board_includes})
    target_compile_definitions(model_loader_model_${_name} PRIVATE ${_board_definitions} ${_model_definitions})
    target_compile_options(model_loader_model_${_name} PRIVATE -O3 -mcmse)
    target_sources(${TARGET_NAME} PRIVATE $<TARGET_OBJECTS:model_loader_model_${_name}>)
    set(_contract_outputs)
    if(MODEL_LOADER_MULTI)
        set(_contract_outputs "${_package}/contract.json" "${_package}/package.json")
    endif()
    add_custom_command(OUTPUT "${_package}/model_expected.hpp" "${_package}/manifest.bin" "${_package}/weights.bin" "${_package}/blob.bin" ${_contract_outputs}
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tool/model.py" package --model-dir "${_model_dir}"
            --object $<TARGET_OBJECTS:model_loader_model_${_name}> --section "${_section}"
            --objcopy "${CMAKE_OBJCOPY}" --objdump "${CMAKE_OBJDUMP}" --output-dir "${_package}"
        DEPENDS model_loader_model_${_name} $<TARGET_OBJECTS:model_loader_model_${_name}>
            "${_model_dir}/weights.bin" "${_model_dir}/contract.json" "${_model_dir}/network.c"
            ${_semantic_dependencies} "${CMAKE_CURRENT_SOURCE_DIR}/tool/contract.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/tool/model.py" "${CMAKE_CURRENT_SOURCE_DIR}/tool/manifest.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/tool/expected.py"
        COMMAND_EXPAND_LISTS VERBATIM)
    list(APPEND _package_dirs "${_package}")
    list(APPEND _package_headers "${_package}/model_expected.hpp")
endforeach()
set(_runtime_library "${STEDGEAI_LIB_DIR}/Lib/GCC/ARMCortexM55/NetworkRuntime${_runtime_version}_CM55_GCC.a")
if(NOT EXISTS "${_runtime_library}")
    message(FATAL_ERROR "Model runtime archive is missing: ${_runtime_library}")
endif()
foreach(_source ecloader ll_aton ll_aton_cipher ll_aton_dbgtrc ll_aton_lib
        ll_aton_lib_sw_operators ll_aton_runtime ll_aton_stai_internal ll_aton_util ll_sw_float ll_sw_integer)
    target_sources(${TARGET_NAME} PRIVATE "${STEDGEAI_LIB_DIR}/Npu/ll_aton/${_source}.c")
endforeach()
set_source_files_properties("${STEDGEAI_LIB_DIR}/Npu/ll_aton/ll_aton_runtime.c" PROPERTIES
    COMPILE_DEFINITIONS "NPU0_IRQHandler=model_load_NPU0_IRQHandler")
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

if(MODEL_LOADER_MULTI)
    add_custom_command(OUTPUT "${_package_root}/model_expected.hpp"
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tool/model.py" catalog
            --packages ${_package_dirs} --output "${_package_root}/model_expected.hpp"
        DEPENDS ${_package_headers} "${CMAKE_CURRENT_SOURCE_DIR}/tool/model.py"
        COMMAND_EXPAND_LISTS VERBATIM)
endif()
add_custom_target(model_loader_model_package DEPENDS "${_package_root}/model_expected.hpp")
add_dependencies(${TARGET_NAME} model_loader_model_package)
target_include_directories(${TARGET_NAME} BEFORE PRIVATE "${_package_root}")
