cmake_minimum_required(VERSION 3.16)
project(alarm_dbus_poc LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../cmake)
include(GdbusCodegen)

# Generate the Alarm bindings in-tree (gitignored) from the Introspect XML.
gdbus_generate(
    BASENAME    gdbus-alarm
    XML         ${CMAKE_CURRENT_SOURCE_DIR}/introspect/es.aleksander.Alarm.xml
    C_NAMESPACE Dbus
    IFC_PREFIX  es.aleksander.
    OUT_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/generated
    OUT_VAR     ALARM_SRC
    EXTRA_FLAGS --generate-c-code gdbus-alarm
)

# Generated Alarm bindings (C, Proxy + Skeleton) -> static lib shared by both sides.
add_library(alarm_iface STATIC ${ALARM_SRC})
target_link_libraries(alarm_iface PUBLIC PkgConfig::GIO)
target_include_directories(alarm_iface PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/generated)

# D-Bus alarm-service (Skeleton).
add_executable(alarm-service service/main.cpp service/alarm-service.cpp)
target_link_libraries(alarm-service PRIVATE alarm_iface PkgConfig::GIO)
