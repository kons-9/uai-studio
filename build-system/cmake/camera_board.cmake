set(UAI_CAMERA_BOARD_APP "")
set(UAI_CAMERA_BOARD_INCLUDE_DIR "")
set(_uai_camera_board_apps
    experiment-model-load
    experiment-ui-control
    experiment-hw-test)

if(APP_TARGET IN_LIST _uai_camera_board_apps)
    set(UAI_CAMERA_BOARD_APP "${APP_TARGET}")
    set(UAI_CAMERA_BOARD_INCLUDE_DIR
        "${CMAKE_SOURCE_DIR}/userspace/${APP_TARGET}/src/camera_runtime/driver/board/include")
endif()
