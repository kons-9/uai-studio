#pragma once

#include <cstdint>

#include <tk/tkernel.h>

#include "driver/cache_driver/cache_driver.hpp"
#include "driver/camera_driver/camera_driver.hpp"
#include "driver/lcd_driver/lcd_driver.hpp"
#include "driver/nor_driver/nor_driver.hpp"
#include "driver/psram_driver/psram_driver.hpp"
#include "driver/rif_driver/rif_driver.hpp"
#include "memory_manager/memory_manager.hpp"
#include "middleware/cpu_task_monitor/cpu_task_monitor.hpp"
#include "task/channels.hpp"

namespace uai::ai::mini {

/* Everything the three tasks share. One instance, created in usermain(). */
struct AppContext {
    memory_manager::MemoryManager memory{};
    middleware::cpu_task_monitor::CpuTaskMonitor cpu_task_monitor{};
    cache::CacheManagement &cache = cache::CacheManagement::Instance();
    psram::PsramManagement &psram = psram::PsramManagement::Instance();
    rif::RifManagement &rif = rif::RifManagement::Instance();
    nor::NorManagement &nor = nor::NorManagement::Instance();
    lcd::LcdManagement &lcd = lcd::LcdManagement::Instance();
    camera::CameraManagement &camera = camera::CameraManagement::Instance();

    InferenceFrameChannel frames{memory};
    InferenceResultChannel results{};

    /* Set once by the initialize task; read by the other tasks. */
    volatile bool external_nor_ready = false;
    ID external_memory_ready = -1;
};

AppContext &App();

} // namespace uai::ai::mini
