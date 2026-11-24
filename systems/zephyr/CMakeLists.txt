# Zephyr service implementations: kernel/filesystem/UI/runtime integration.
include(${ASTER_ROOT}/common/sources.cmake)
target_include_directories(app PRIVATE ${ASTER_COMMON_INCLUDE_DIRS})
target_sources(app PRIVATE ${ASTER_COMMON_DEVICE_SOURCES} ${ASTER_COMMON_OS_SOURCES} port/device.c port/os.c port/power.c)
target_sources(app PRIVATE ${ASTER_COMMON_SERVICE_SOURCES} ${ASTER_ROOT}/systems/shared/port/services.c)
target_sources(app PRIVATE ${ASTER_COMMON_PORT_CHECK})
target_sources(app PRIVATE ${ASTER_COMMON_ROOT}/audio/aster_player_service.c)
target_sources(app PRIVATE ${ASTER_COMMON_DEPLOY})
target_sources(app PRIVATE ${ASTER_COMMON_NETWORK_PORT} ${ASTER_ROOT}/systems/shared/port/network.c)
target_compile_definitions(app PRIVATE ASTER_DIRTY_CAP=${CONFIG_ASTER_GFX_DIRTY_REGIONS})
add_subdirectory(core)
add_subdirectory(services)
add_subdirectory(shell)
if(CONFIG_ASTER_LVGL)
  add_subdirectory(ui)
endif()
if(CONFIG_ASTER_WAMR)
  add_subdirectory(runtime)
endif()
target_sources(app PRIVATE src/main.c)
target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/core
  ${CMAKE_CURRENT_SOURCE_DIR}/services
  ${CMAKE_CURRENT_SOURCE_DIR}/runtime
  ${CMAKE_CURRENT_SOURCE_DIR}/ui
  ${ASTER_ROOT}/platform
  ${ASTER_ABI_INC})
