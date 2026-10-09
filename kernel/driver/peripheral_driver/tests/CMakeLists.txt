uai_add_host_test(peripheral_driver_test
    peripheral_driver_test.cpp
    ../peripheral_driver.cpp)
target_include_directories(peripheral_driver_test BEFORE PRIVATE fixtures)