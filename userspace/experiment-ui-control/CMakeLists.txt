cmake_minimum_required(VERSION 3.16)
set(FEATURE_CONSTRAINTS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(FEATURE_CONSTRAINTS_CLI "${FEATURE_CONSTRAINTS_ROOT}/tool/feature_constraints/cli.py")
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	set(EXPERIMENT_EXTENSION "${CMAKE_CURRENT_SOURCE_DIR}/src/firmware.cpp")
	include("${CMAKE_CURRENT_LIST_DIR}/firmware.cmake")
	add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/features_generated.hpp"
		COMMAND ${Python3_EXECUTABLE} -m tool.feature_constraints generate
			"${CMAKE_CURRENT_SOURCE_DIR}/config/features.json" --output "${CMAKE_CURRENT_BINARY_DIR}/features_generated.hpp"
		DEPENDS "${FEATURE_CONSTRAINTS_CLI}" config/features.json
		WORKING_DIRECTORY "${FEATURE_CONSTRAINTS_ROOT}" VERBATIM)
	target_sources(${TARGET_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/features_generated.hpp"
		"${COMPONENTS}/gt911/gt911.c" "${COMPONENTS}/gt911/gt911_reg.c")
	target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${COMPONENTS}/gt911")
	target_link_options(${TARGET_NAME} BEFORE PRIVATE "-T${CMAKE_CURRENT_SOURCE_DIR}/panel.ld")
	set_property(TARGET ${TARGET_NAME} APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/panel.ld")
	return()
endif()
project(experiment_ui_control_tests LANGUAGES CXX)
enable_testing()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/features_generated.hpp"
	COMMAND ${Python3_EXECUTABLE} -m tool.feature_constraints generate
		"${CMAKE_CURRENT_SOURCE_DIR}/config/features.json" --output "${CMAKE_CURRENT_BINARY_DIR}/features_generated.hpp"
	DEPENDS "${FEATURE_CONSTRAINTS_CLI}" config/features.json
	WORKING_DIRECTORY "${FEATURE_CONSTRAINTS_ROOT}" VERBATIM)
add_executable(app_state_test tests/app_state_test.cpp "${CMAKE_CURRENT_BINARY_DIR}/features_generated.hpp")
target_include_directories(app_state_test PRIVATE src src/camera_runtime "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_features(app_state_test PRIVATE cxx_std_17)
target_compile_options(app_state_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME app_state COMMAND app_state_test)
add_executable(periodic_test tests/periodic_test.cpp)
target_include_directories(periodic_test PRIVATE src)
target_compile_features(periodic_test PRIVATE cxx_std_17)
target_compile_options(periodic_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME periodic COMMAND periodic_test)
add_test(NAME features COMMAND ${Python3_EXECUTABLE} -m unittest discover -s "${FEATURE_CONSTRAINTS_ROOT}/tool/feature_constraints/tests")
