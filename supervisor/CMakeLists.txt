cmake_minimum_required(VERSION 3.16)
project(supervisor_dbus_poc LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
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

# --- Generated bindings: one codegen lib per introspect XML (ADR 0002) ---------

# Daemon's own interface: proxy (Supervisor consumes) + skeleton (daemon exports).
gdbus_generate(
    BASENAME    gdbus-poc-daemon
    XML         ${CMAKE_CURRENT_SOURCE_DIR}/introspect/org.poc.Daemon.xml
    C_NAMESPACE GDbusPoc
    IFC_PREFIX  org.poc.
    OUT_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/generated
    OUT_VAR     POC_DAEMON_SRC
)

# systemd Manager: proxy only (Supervisor consumes).
gdbus_generate(
    BASENAME    gdbus-systemd-manager
    XML         ${CMAKE_CURRENT_SOURCE_DIR}/introspect/org.freedesktop.systemd1.Manager.xml
    C_NAMESPACE GDbus
    IFC_PREFIX  org.freedesktop.
    OUT_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/generated
    OUT_VAR     SYSTEMD_MANAGER_SRC
)

# systemd Unit + Service (one XML, two interfaces): proxies the UnitMonitor uses.
gdbus_generate(
    BASENAME    gdbus-systemd-unit
    XML         ${CMAKE_CURRENT_SOURCE_DIR}/introspect/org.freedesktop.systemd1.Unit.xml
    C_NAMESPACE GDbus
    IFC_PREFIX  org.freedesktop.
    OUT_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/generated
    OUT_VAR     SYSTEMD_UNIT_SRC
)

# Each generated C file -> its own static lib, include dir PUBLIC for bare include.
add_library(poc_daemon_iface STATIC ${POC_DAEMON_SRC})
target_link_libraries(poc_daemon_iface PUBLIC PkgConfig::GIO)
target_include_directories(poc_daemon_iface PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/generated)

add_library(systemd_manager_iface STATIC ${SYSTEMD_MANAGER_SRC})
target_link_libraries(systemd_manager_iface PUBLIC PkgConfig::GIO)
target_include_directories(systemd_manager_iface PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/generated)

add_library(systemd_unit_iface STATIC ${SYSTEMD_UNIT_SRC})
target_link_libraries(systemd_unit_iface PUBLIC PkgConfig::GIO)
target_include_directories(systemd_unit_iface PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/generated)

# --- Executables ---------------------------------------------------------------

# Dummy daemon: exports org.poc.Daemon, Type=notify, watchdog ping, fault trigger.
add_executable(poc-daemon daemon/dummy-daemon.cpp)
target_link_libraries(poc-daemon PRIVATE poc_daemon_iface PkgConfig::GIO)

# Supervisor: observes systemd + each daemon's State; revives on clean stop.
add_executable(supervisor
    supervisor/main.cpp
    supervisor/supervisor.cpp
    supervisor/unit-monitor.cpp
    supervisor/systemd-enums.cpp
    supervisor/daemon-state-watcher.cpp
)
target_link_libraries(supervisor PRIVATE systemd_manager_iface systemd_unit_iface poc_daemon_iface PkgConfig::GIO)

# --- Install + unit template ---------------------------------------------------

include(GNUInstallDirs)
install(TARGETS poc-daemon supervisor RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Bake the installed daemon path into the template unit; %i selects the instance.
set(DAEMON_PATH "${CMAKE_INSTALL_FULL_BINDIR}/poc-daemon")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/daemon/poc-dummy@.service.in"
    "${CMAKE_CURRENT_BINARY_DIR}/poc-dummy@.service"
    @ONLY
)

set(USER_UNIT_DIR "$ENV{HOME}/.config/systemd/user")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/poc-dummy@.service" DESTINATION "${USER_UNIT_DIR}")
install(CODE "message(STATUS \"Installed. Run: systemctl --user daemon-reload\")")
