cmake_minimum_required(VERSION 3.16)
project(systemd_dbus_poc LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../cmake)
include(GdbusCodegen)

# Default to a rootless user prefix (ADR 0001); override with -DCMAKE_INSTALL_PREFIX.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local" CACHE PATH "Install prefix" FORCE)
endif()

# Generate the Manager proxy in-tree (gitignored) from the Introspect XML.
gdbus_generate(
    BASENAME    gdbus-systemd-manager
    XML         ${CMAKE_CURRENT_SOURCE_DIR}/introspect/org.freedesktop.systemd1.Manager.xml
    C_NAMESPACE GDbus
    IFC_PREFIX  org.freedesktop.
    OUT_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/generated
    OUT_VAR     SYSTEMD_MANAGER_SRC
    EXTRA_FLAGS --generate-c-code gdbus-systemd
)

# Generated Manager proxy (C) -> static lib, include dir PUBLIC for bare include.
add_library(systemd_manager STATIC ${SYSTEMD_MANAGER_SRC})
target_link_libraries(systemd_manager PUBLIC PkgConfig::GIO)
target_include_directories(systemd_manager PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/generated)

# Dummy daemon supervised by systemd.
add_executable(systemd-daemon daemon/dummy-daemon.cpp)

# D-Bus client driving start/stop/restart + ListUnits.
add_executable(systemd-client client/main.cpp client/systemd-manager.cpp)
target_link_libraries(systemd-client PRIVATE systemd_manager PkgConfig::GIO)

# Install the daemon and bake its absolute path into the user unit.
include(GNUInstallDirs)
install(TARGETS systemd-daemon RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

set(DAEMON_PATH "${CMAKE_INSTALL_FULL_BINDIR}/systemd-daemon")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/daemon/poc-dummy.service.in"
    "${CMAKE_CURRENT_BINARY_DIR}/poc-dummy.service"
    @ONLY
)

set(USER_UNIT_DIR "$ENV{HOME}/.config/systemd/user")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/poc-dummy.service" DESTINATION "${USER_UNIT_DIR}")
install(CODE "message(STATUS \"Installed. Run: systemctl --user daemon-reload\")")
