# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(VERSION_PRIMARY   1)
set(VERSION_SECONDARY 1)

# Get Git commit hash and dirty state
find_package(Git)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        OUTPUT_VARIABLE GIT_SHORT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} diff --quiet
        RESULT_VARIABLE GIT_DIRTY
    )
    if(GIT_DIRTY EQUAL 1)
        set(GIT_DIRTY_STR "-dirty")
    else()
        set(GIT_DIRTY_STR "")
    endif()
else()
    set(GIT_SHORT_HASH "unknown")
    set(GIT_DIRTY_STR "")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(serial2ip_esp32)

# Set version number
set(APP_VERSION "v${VERSION_PRIMARY}.${VERSION_SECONDARY}-g${GIT_SHORT_HASH}${GIT_DIRTY_STR}")
message(STATUS "Project version: ${APP_VERSION}")

# Get current date in YYYYMMDD format
string(TIMESTAMP BUILD_DATE "%Y%m%d" UTC)
message(STATUS "Build date: ${BUILD_DATE}")

idf_component_get_property(main main COMPONENT_LIB)
target_compile_definitions(${main} PRIVATE
    "APP_VERSION=\"${APP_VERSION}\""
    "BUILD_DATE=\"${BUILD_DATE}\""
)
