set(UAI_CAMERA_BOARD_APP "")
set(UAI_CAMERA_BOARD_INCLUDE_DIR "")
set(_uai_camera_board_apps
    experiment-camera-lcd
    experiment-camera-pipe2
    experiment-camera-lcd-touch
    experiment-camera-control
    experiment-gpu
    experiment-model-load
    experiment-ui-control)

if(APP_TARGET IN_LIST _uai_camera_board_apps)
    set(UAI_CAMERA_BOARD_APP "${APP_TARGET}")
    if(APP_TARGET MATCHES "^(experiment-gpu|experiment-model-load|experiment-ui-control)$")
        set(UAI_CAMERA_BOARD_INCLUDE_DIR
            "${CMAKE_SOURCE_DIR}/userspace/${APP_TARGET}/src/camera_runtime/driver/board/include")
    else()
        set(UAI_CAMERA_BOARD_INCLUDE_DIR
            "${CMAKE_SOURCE_DIR}/userspace/${APP_TARGET}/src/driver/board/include")
    endif()
endif()
