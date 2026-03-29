cmake_minimum_required(VERSION 3.10)
project(TypeWar VERSION 1.0 LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)

add_executable(tw src/tw.c)
target_compile_features(tw PRIVATE c_std_99)
target_link_libraries(tw PRIVATE PkgConfig::GTK4 m)
