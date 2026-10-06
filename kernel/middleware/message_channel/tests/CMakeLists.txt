uai_add_host_test(fixed_message_slots_test fixed_message_slots_test.cpp)
uai_add_host_test(message_channel_test message_channel_test.cpp)
target_include_directories(message_channel_test BEFORE PRIVATE
    "${UAI_ROOT}/kernel/utkernel/linux/include")