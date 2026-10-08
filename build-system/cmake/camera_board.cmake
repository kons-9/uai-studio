set(UAI_CAMERA_BOARD_APP "")
set(_uai_camera_board_apps
    experiment-camera-lcd
    experiment-camera-pipe2
    experiment-camera-lcd-touch)

if(APP_TARGET IN_LIST _uai_camera_board_apps)
    set(UAI_CAMERA_BOARD_APP "${APP_TARGET}")
elseif(APP_TARGET STREQUAL "experiment-camera-control")
    set(UAI_CAMERA_BOARD_APP "experiment-camera-pipe2")
endif()