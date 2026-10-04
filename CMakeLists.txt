cmake_minimum_required(VERSION 3.16)
project(gdbus_test LANGUAGES C CXX)

# Each step is a standalone sub-project; this only aggregates them.
add_subdirectory(alarm)
add_subdirectory(systemd)
