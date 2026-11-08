cmake_minimum_required(VERSION 3.16)
project(LisaOS VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- FLTK 1.4 -------------------------------------------------------------
# Najpierw szukamy zainstalowanego FLTK 1.4 (np. -DFLTK_DIR=/opt/fltk/share/fltk).
# Jeśli go nie ma, pobieramy i budujemy statycznie wersję 1.4.4.
find_package(FLTK 1.4 CONFIG QUIET)
if(NOT FLTK_FOUND)
  message(STATUS "LisaOS: FLTK 1.4 nie znaleziony - pobieram release-1.4.4")
  include(FetchContent)
  set(FLTK_BUILD_TEST OFF CACHE BOOL "" FORCE)
  set(FLTK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(FLTK_BUILD_FLUID OFF CACHE BOOL "" FORCE)
  set(FLTK_BUILD_FLTK_OPTIONS OFF CACHE BOOL "" FORCE)
  set(FLTK_BUILD_GL OFF CACHE BOOL "" FORCE)
  set(FLTK_USE_PANGO OFF CACHE BOOL "" FORCE)
  option(LISAOS_WAYLAND "Buduj FLTK z backendem Wayland" OFF)
  set(FLTK_BACKEND_WAYLAND ${LISAOS_WAYLAND} CACHE BOOL "" FORCE)
  FetchContent_Declare(fltk
    GIT_REPOSITORY https://github.com/fltk/fltk.git
    GIT_TAG release-1.4.4
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(fltk)
endif()

# --- libplatinum: wygląd i chrome okien wspólny dla wszystkich komponentów --
add_library(lisaos-platinum STATIC
  platinum/platinum.cxx
  platinum/pwindow.cxx
  platinum/anim.cxx)
find_package(X11 REQUIRED)
target_include_directories(lisaos-platinum PUBLIC platinum)
target_link_libraries(lisaos-platinum PUBLIC fltk::fltk X11::X11)

# --- denise: menedżer plików ---------------------------------------------------------
add_executable(lisaos-denise
  denise/main.cxx
  denise/desktop.cxx
  denise/folder_view.cxx
  denise/spatial.cxx
  denise/appimage.cxx)
target_link_libraries(lisaos-denise PRIVATE lisaos-platinum fltk::images)

# --- insight: menedżer okien X11 ----------------------------------------------
add_executable(lisaos-insight insight/main.cxx)
target_link_libraries(lisaos-insight PRIVATE lisaos-platinum X11::X11)

install(TARGETS lisaos-denise lisaos-insight RUNTIME DESTINATION bin)
install(PROGRAMS session/lisaos-session DESTINATION bin)
install(FILES session/lisaos.desktop DESTINATION share/xsessions)
