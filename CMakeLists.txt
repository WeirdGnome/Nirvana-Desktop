# Rambler for X11 - a port of the AROS desktop (AROS Public License 1.1, see LICENSE)
cmake_minimum_required(VERSION 3.16)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" RAMBLER_VERSION LIMIT_COUNT 1)
project(RamblerX11 VERSION ${RAMBLER_VERSION} LANGUAGES C CXX)
add_compile_definitions(RAMBLER_VERSION="${RAMBLER_VERSION}")
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

# Lune on FLTK
add_library(lune STATIC rambler/lune/pens.cxx rambler/lune/frame.cxx rambler/lune/lune.cxx rambler/lune/catalog.cxx rambler/lune/imspec.cxx rambler/lune/fontreq.cxx rambler/lune/calendar.cxx)
target_include_directories(lune PUBLIC rambler/lune)
target_link_libraries(lune PUBLIC fltk::fltk fltk::images)

# a test window with every built-in frame
add_executable(lune-frames rambler/lune/test_frames.cxx)
target_link_libraries(lune-frames lune)
add_executable(lune-test rambler/lune/test_lune.cxx)
target_link_libraries(lune-test lune)

# Rambler
add_library(rambler-classes STATIC rambler/iconlist.cxx rambler/icons.cxx rambler/iconwindow.cxx rambler/tools.cxx rambler/prefs.cxx rambler/fileops.cxx rambler/guisettings.cxx rambler/find.cxx rambler/inputprefs.cxx rambler/screenmodeprefs.cxx rambler/localeprefs.cxx rambler/pointerprefs.cxx rambler/fontprefs.cxx rambler/networkprefs.cxx rambler/diskobject.cxx rambler/appimage.cxx rambler/icontrolprefs.cxx rambler/timeprefs.cxx rambler/soundprefs.cxx)
target_include_directories(rambler-classes PUBLIC rambler)
target_link_libraries(rambler-classes PUBLIC lune)
# the colour icons' ARGB images are zlib packed (without zlib those icons show their other images)
find_package(ZLIB)
if(ZLIB_FOUND)
    target_compile_definitions(rambler-classes PRIVATE RAMBLER_HAVE_ZLIB)
    target_link_libraries(rambler-classes PUBLIC ZLIB::ZLIB)
endif()
add_executable(iconlist-test rambler/test_iconlist.cxx)
target_link_libraries(iconlist-test rambler-classes)
add_executable(rambler rambler/rambler.cxx)
target_link_libraries(rambler rambler-classes)
install(TARGETS rambler RUNTIME DESTINATION bin COMPONENT rambler)

# the window manager (Intuition's part: screens, window frames, gadgets)
find_package(X11 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XFT REQUIRED xft)
add_executable(rambler-wm wm/wm.cxx)
target_include_directories(rambler-wm PRIVATE ${XFT_INCLUDE_DIRS})
target_link_libraries(rambler-wm ${XFT_LIBRARIES} ${X11_LIBRARIES} ${X11_Xext_LIB})
install(TARGETS rambler-wm RUNTIME DESTINATION bin COMPONENT rambler)

# the session for the display manager
install(PROGRAMS session/rambler-session DESTINATION bin COMPONENT rambler)
set(RAMBLER_XSESSIONS_DIR "/usr/share/xsessions" CACHE PATH "where display managers look for sessions")
install(FILES session/rambler.desktop DESTINATION ${RAMBLER_XSESSIONS_DIR} COMPONENT rambler)
install(FILES LICENSE README.md DESTINATION share/doc/rambler-x11 COMPONENT rambler)

# the catalogs: next to the programs in the build tree, installed into share/rambler/catalogs
file(GLOB RAMBLER_CATALOGS ${CMAKE_CURRENT_SOURCE_DIR}/catalogs/*.catalog)
foreach(cat ${RAMBLER_CATALOGS})   # (copied again whenever one changes)
    get_filename_component(cat_name ${cat} NAME)
    configure_file(${cat} ${CMAKE_CURRENT_BINARY_DIR}/catalogs/${cat_name} COPYONLY)
endforeach()
install(FILES ${RAMBLER_CATALOGS} DESTINATION share/rambler/catalogs COMPONENT rambler)
