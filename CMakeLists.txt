# Wanderer for X11 - a port of the AROS desktop (AROS Public License 1.1, see LICENSE)
cmake_minimum_required(VERSION 3.16)
project(WandererX11 VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# FLTK 1.4: an installed one, or 1.4.4 built statically
find_package(FLTK 1.4 CONFIG QUIET)
if(NOT FLTK_FOUND)
  include(FetchContent)
  set(FLTK_BUILD_TEST OFF CACHE BOOL "" FORCE)
  set(FLTK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(FLTK_BUILD_FLUID OFF CACHE BOOL "" FORCE)
  set(FLTK_BUILD_FLTK_OPTIONS OFF CACHE BOOL "" FORCE)
  set(FLTK_BUILD_GL OFF CACHE BOOL "" FORCE)
  set(FLTK_USE_PANGO OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(fltk GIT_REPOSITORY https://github.com/fltk/fltk.git GIT_TAG release-1.4.4 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(fltk)
endif()

# Zune on FLTK
add_library(zune STATIC wanderer/zune/pens.cxx wanderer/zune/frame.cxx wanderer/zune/zune.cxx)
target_include_directories(zune PUBLIC wanderer/zune)
target_link_libraries(zune PUBLIC fltk::fltk)

# a test window with every built-in frame
add_executable(zune-frames wanderer/zune/test_frames.cxx)
target_link_libraries(zune-frames zune)
add_executable(zune-test wanderer/zune/test_zune.cxx)
target_link_libraries(zune-test zune)

# Wanderer
add_library(wanderer-classes STATIC wanderer/iconlist.cxx wanderer/iconwindow.cxx wanderer/tools.cxx)
target_include_directories(wanderer-classes PUBLIC wanderer)
target_link_libraries(wanderer-classes PUBLIC zune)
add_executable(iconlist-test wanderer/test_iconlist.cxx)
target_link_libraries(iconlist-test wanderer-classes)
add_executable(wanderer wanderer/wanderer.cxx)
target_link_libraries(wanderer wanderer-classes)
install(TARGETS wanderer RUNTIME DESTINATION bin)
