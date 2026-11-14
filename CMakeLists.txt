# Wanderer for X11 - a port of the AROS desktop (AROS Public License 1.1, see LICENSE)
cmake_minimum_required(VERSION 3.16)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" WANDERER_VERSION LIMIT_COUNT 1)
project(WandererX11 VERSION ${WANDERER_VERSION} LANGUAGES C CXX)
add_compile_definitions(WANDERER_VERSION="${WANDERER_VERSION}")
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
  set(FLTK_BACKEND_WAYLAND OFF CACHE BOOL "" FORCE)   # X11 only: the window manager is an X11 one
  set(FLTK_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
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
add_library(wanderer-classes STATIC wanderer/iconlist.cxx wanderer/icons.cxx wanderer/iconwindow.cxx wanderer/tools.cxx wanderer/prefs.cxx)
target_include_directories(wanderer-classes PUBLIC wanderer)
target_link_libraries(wanderer-classes PUBLIC zune)
add_executable(iconlist-test wanderer/test_iconlist.cxx)
target_link_libraries(iconlist-test wanderer-classes)
add_executable(wanderer wanderer/wanderer.cxx)
target_link_libraries(wanderer wanderer-classes)
install(TARGETS wanderer RUNTIME DESTINATION bin COMPONENT wanderer)

# the window manager (Intuition's part: screens, window frames, gadgets)
find_package(X11 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XFT REQUIRED xft)
add_executable(wanderer-wm wm/wm.cxx)
target_include_directories(wanderer-wm PRIVATE ${XFT_INCLUDE_DIRS})
target_link_libraries(wanderer-wm ${XFT_LIBRARIES} ${X11_LIBRARIES})
install(TARGETS wanderer-wm RUNTIME DESTINATION bin COMPONENT wanderer)

# the session for the display manager
install(PROGRAMS session/wanderer-session DESTINATION bin COMPONENT wanderer)
set(WANDERER_XSESSIONS_DIR "/usr/share/xsessions" CACHE PATH "where display managers look for sessions")
install(FILES session/wanderer.desktop DESTINATION ${WANDERER_XSESSIONS_DIR} COMPONENT wanderer)
install(FILES LICENSE README.md DESTINATION share/doc/wanderer-x11 COMPONENT wanderer)
