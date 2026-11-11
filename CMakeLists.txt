cmake_minimum_required(VERSION 3.16)
# The version is in the VERSION file - raised with every released package.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" NIRVANA_VERSION LIMIT_COUNT 1)
project(NirvanaDesktop VERSION ${NIRVANA_VERSION} LANGUAGES C CXX)
add_compile_definitions(NIRVANA_VERSION="${NIRVANA_VERSION}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- FLTK 1.4 -------------------------------------------------------------
# First look for an installed FLTK 1.4 (e.g. -DFLTK_DIR=/opt/fltk/share/fltk).
# If there is none, download and build version 1.4.4 statically.
find_package(FLTK 1.4 CONFIG QUIET)
if(NOT FLTK_FOUND)
  message(STATUS "Nirvana Desktop: FLTK 1.4 not found - downloading release-1.4.4")
  include(FetchContent)
  set(FLTK_BUILD_TEST OFF CACHE BOOL "" FORCE)
  set(FLTK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(FLTK_BUILD_FLUID OFF CACHE BOOL "" FORCE)
  set(FLTK_BUILD_FLTK_OPTIONS OFF CACHE BOOL "" FORCE)
  set(FLTK_BUILD_GL OFF CACHE BOOL "" FORCE)
  set(FLTK_USE_PANGO OFF CACHE BOOL "" FORCE)
  option(NIRVANA_WAYLAND "Buduj FLTK z backendem Wayland" OFF)
  set(FLTK_BACKEND_WAYLAND ${NIRVANA_WAYLAND} CACHE BOOL "" FORCE)
  FetchContent_Declare(fltk
    GIT_REPOSITORY https://github.com/fltk/fltk.git
    GIT_TAG release-1.4.4
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(fltk)
endif()

# --- translations (gettext): library and .mo catalogs from po/*.po ----------
add_library(nirvana-i18n STATIC platinum/i18n.cxx)
target_include_directories(nirvana-i18n PUBLIC platinum)
find_program(MSGFMT msgfmt)
file(GLOB NIRVANA_PO po/*.po)
set(NIRVANA_MO)
if(MSGFMT)
  foreach(po ${NIRVANA_PO})
    get_filename_component(lang ${po} NAME_WE)
    set(mo ${CMAKE_BINARY_DIR}/locale/${lang}/LC_MESSAGES/nirvana.mo)
    add_custom_command(OUTPUT ${mo}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/locale/${lang}/LC_MESSAGES
      COMMAND ${MSGFMT} --check -o ${mo} ${po}
      DEPENDS ${po} COMMENT "Translation: ${lang}")
    list(APPEND NIRVANA_MO ${mo})
    # share/nirvana/locale, not share/locale: localepurge and "path-exclude" in
    # minimal installations delete .mo files of languages not chosen
    install(FILES ${mo} DESTINATION share/nirvana/locale/${lang}/LC_MESSAGES COMPONENT nirvana)
  endforeach()
else()
  message(WARNING "msgfmt not found (gettext package) - the interface will be English only")
endif()
add_custom_target(nirvana-translations ALL DEPENDS ${NIRVANA_MO})

# --- libplatinum: look and window chrome shared by all components ----------
add_library(nirvana-platinum STATIC
  platinum/platinum.cxx
  platinum/pwindow.cxx
  platinum/anim.cxx
  platinum/theme.cxx
  platinum/wallpaper.cxx
  platinum/icons_wb.cxx
  platinum/framewin.cxx)
find_package(X11 REQUIRED)
target_include_directories(nirvana-platinum PUBLIC platinum)
target_link_libraries(nirvana-platinum PUBLIC nirvana-i18n fltk::fltk fltk::images X11::X11 X11::Xext)

# --- nirvana-net: networking via NetworkManager (nmcli + D-Bus) -------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(DBUS REQUIRED IMPORTED_TARGET dbus-1)
add_library(nirvana-net STATIC net/net.cxx)
target_include_directories(nirvana-net PUBLIC net)
target_link_libraries(nirvana-net PUBLIC nirvana-i18n fltk::fltk PkgConfig::DBUS)

# --- nirvana-audio and nirvana-power: volume and power applets ---------------
pkg_check_modules(XSS REQUIRED IMPORTED_TARGET xscrnsaver)
add_library(nirvana-audio STATIC audio/audio.cxx)
target_include_directories(nirvana-audio PUBLIC audio)
target_link_libraries(nirvana-audio PUBLIC nirvana-i18n fltk::fltk)
add_library(nirvana-power STATIC power/power.cxx)
target_include_directories(nirvana-power PUBLIC power)
target_link_libraries(nirvana-power PUBLIC nirvana-i18n fltk::fltk PkgConfig::DBUS PkgConfig::XSS X11::Xext X11::X11)

# --- denise: file manager ----------------------------------------------------
add_executable(nirvana-denise
  denise/main.cxx
  denise/desktop.cxx
  denise/folder_view.cxx
  denise/spatial.cxx
  denise/appimage.cxx
  denise/gary.cxx
  denise/volumes.cxx
  denise/applets.cxx
  denise/menus.cxx
  denise/tray.cxx)
target_link_libraries(nirvana-denise PRIVATE nirvana-platinum nirvana-net nirvana-audio nirvana-power fltk::images PkgConfig::DBUS)

# --- insight: X11 window manager ---------------------------------------------
add_executable(nirvana-insight insight/main.cxx)
target_link_libraries(nirvana-insight PRIVATE nirvana-platinum X11::X11)

# --- alice: control panels ---------------------------------------------------
add_executable(nirvana-alice alice/main.cxx alice/appearance.cxx alice/network.cxx alice/power.cxx alice/preferences.cxx)
target_link_libraries(nirvana-alice PRIVATE nirvana-platinum nirvana-net nirvana-power)

# --- Nirvana Shell: terminal emulator (system libvterm or the bundled one) ---
pkg_check_modules(VTERM IMPORTED_TARGET vterm>=0.3)
if(VTERM_FOUND)
  set(NIRVANA_VTERM PkgConfig::VTERM)
else()
  file(GLOB VTERM_SRC third_party/libvterm/src/*.c)
  add_library(nirvana-vterm STATIC ${VTERM_SRC})
  set_target_properties(nirvana-vterm PROPERTIES C_STANDARD 99)
  target_include_directories(nirvana-vterm PUBLIC third_party/libvterm/include)
  set(NIRVANA_VTERM nirvana-vterm)
endif()
add_executable(nirvana-shell shell/main.cxx)
target_link_libraries(nirvana-shell PRIVATE nirvana-platinum ${NIRVANA_VTERM} util)

# Everything in the "nirvana" component - the .deb package does not take FLTK files
# (FetchContent adds its own install rules for headers and libraries).
install(TARGETS nirvana-denise nirvana-insight nirvana-alice nirvana-shell RUNTIME DESTINATION bin COMPONENT nirvana)
install(DIRECTORY schemes/ DESTINATION share/nirvana/schemes COMPONENT nirvana)
install(DIRECTORY wallpapers/ DESTINATION share/nirvana/wallpapers COMPONENT nirvana)
install(PROGRAMS session/nirvana-session session/nirvana-autostart DESTINATION bin COMPONENT nirvana)
install(FILES session/nirvana.desktop DESTINATION share/xsessions COMPONENT nirvana)
install(FILES session/nirvana-portals.conf DESTINATION share/xdg-desktop-portal COMPONENT nirvana)
install(FILES README.md README.pl.md DESTINATION share/doc/nirvana COMPONENT nirvana)

# --- .deb package (Devuan 6 "Excalibur" / Debian 13): cpack -G DEB ----------
# Library dependencies come from dpkg-shlibdeps; the rest (programs Nirvana Desktop
# uses at run time) is listed by hand. apt installs Recommends by default and
# simply skips Recommends that are missing from the repositories.
set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME nirvana)
set(CPACK_PACKAGE_VENDOR "Nirvana Desktop")
set(CPACK_PACKAGE_CONTACT "Nirvana Desktop <nirvana@localhost>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "lightweight desktop in the spirit of Mac OS 9 and the Amiga")
set(CPACK_PACKAGE_DESCRIPTION "Nirvana Desktop is an X11 session built with FLTK: the Insight window manager,
the Rambler desktop and file manager, an application switcher, control panels (Alice)
and the Nirvana Shell terminal. English and Polish interface.")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};NirvanaDesktop;nirvana;/")
set(CPACK_DEBIAN_PACKAGE_VERSION "${NIRVANA_VERSION}")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION x11)
set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "dbus-bin | dbus")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS
  "xserver-xorg, xinit, fonts-dejavu-core, libpam-elogind | elogind, polkitd, lxpolkit, \
udisks2, network-manager, pulseaudio-utils, alsa-utils, squashfs-tools, bubblewrap, xsecurelock, xdg-utils, \
xdg-desktop-portal, xdg-desktop-portal-gtk")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS
  "lightdm, network-manager-openvpn, network-manager-vpnc, wireguard-tools, power-profiles-daemon, pipewire-pulse")
include(CPack)
