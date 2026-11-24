# The same project property is available during IDF dependency expansion and
# normal component registration. Board declarations must work in both phases.
idf_build_get_property(aster_project_dir PROJECT_DIR)
set(ASTER_IDF_ADAPTERS "${CMAKE_CURRENT_LIST_DIR}/adapters")
set(ASTER_PLATFORM_AUDIO "${CMAKE_CURRENT_LIST_DIR}/../../platform/esp32s3")
include("${aster_project_dir}/aster_board.cmake")
