cmake_minimum_required(VERSION 3.16)
project(NirvanaDesktop VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- FLTK 1.4 -------------------------------------------------------------
# Najpierw szukamy zainstalowanego FLTK 1.4 (np. -DFLTK_DIR=/opt/fltk/share/fltk).
# Jeśli go nie ma, pobieramy i budujemy statycznie wersję 1.4.4.
find_package(FLTK 1.4 CONFIG QUIET)
if(NOT FLTK_FOUND)
  message(STATUS "Nirvana Desktop: FLTK 1.4 nie znaleziony - pobieram release-1.4.4")
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

# --- tłumaczenia (gettext): biblioteka i katalogi .mo z po/*.po ---------------
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
      DEPENDS ${po} COMMENT "Tłumaczenie: ${lang}")
    list(APPEND NIRVANA_MO ${mo})
    # share/nirvana/locale, nie share/locale: localepurge i "path-exclude" w
    # minimalnych instalacjach usuwają pliki .mo spoza wybranych języków
    install(FILES ${mo} DESTINATION share/nirvana/locale/${lang}/LC_MESSAGES COMPONENT nirvana)
  endforeach()
else()
  message(WARNING "Brak msgfmt (pakiet gettext) - interfejs będzie tylko po polsku")
endif()
add_custom_target(nirvana-translations ALL DEPENDS ${NIRVANA_MO})

# --- libplatinum: wygląd i chrome okien wspólny dla wszystkich komponentów --
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

# --- nirvana-net: sieć przez NetworkManager (nmcli + D-Bus) --------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(DBUS REQUIRED IMPORTED_TARGET dbus-1)
add_library(nirvana-net STATIC net/net.cxx)
target_include_directories(nirvana-net PUBLIC net)
target_link_libraries(nirvana-net PUBLIC nirvana-i18n fltk::fltk PkgConfig::DBUS)

# --- nirvana-audio i nirvana-power: aplety głośności i zasilania -------------------
pkg_check_modules(XSS REQUIRED IMPORTED_TARGET xscrnsaver)
add_library(nirvana-audio STATIC audio/audio.cxx)
target_include_directories(nirvana-audio PUBLIC audio)
target_link_libraries(nirvana-audio PUBLIC nirvana-i18n fltk::fltk)
add_library(nirvana-power STATIC power/power.cxx)
target_include_directories(nirvana-power PUBLIC power)
target_link_libraries(nirvana-power PUBLIC nirvana-i18n fltk::fltk PkgConfig::DBUS PkgConfig::XSS X11::Xext X11::X11)

# --- denise: menedżer plików ---------------------------------------------------------
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

# --- insight: menedżer okien X11 ----------------------------------------------
add_executable(nirvana-insight insight/main.cxx)
target_link_libraries(nirvana-insight PRIVATE nirvana-platinum X11::X11)

# --- alice: panele sterowania (Wygląd) -------------------------------------------
add_executable(nirvana-alice alice/main.cxx alice/wyglad.cxx alice/siec.cxx alice/zasilanie.cxx alice/ustawienia.cxx)
target_link_libraries(nirvana-alice PRIVATE nirvana-platinum nirvana-net nirvana-power)

# --- Nirvana Shell: emulator terminala (libvterm systemowy albo dołączony) --------
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

# Wszystko z komponentem "nirvana" - paczka .deb nie zabierze plików FLTK
# (FetchContent dokłada własne reguły install dla nagłówków i bibliotek).
install(TARGETS nirvana-denise nirvana-insight nirvana-alice nirvana-shell RUNTIME DESTINATION bin COMPONENT nirvana)
install(DIRECTORY schematy/ DESTINATION share/nirvana/schematy COMPONENT nirvana)
install(DIRECTORY tapety/ DESTINATION share/nirvana/tapety COMPONENT nirvana)
install(PROGRAMS session/nirvana-session session/nirvana-autostart DESTINATION bin COMPONENT nirvana)
install(PROGRAMS session/lisaos-session DESTINATION bin COMPONENT nirvana)   # stare ~/.xinitrc
install(FILES session/nirvana.desktop DESTINATION share/xsessions COMPONENT nirvana)
install(FILES session/nirvana-portals.conf DESTINATION share/xdg-desktop-portal COMPONENT nirvana)
install(FILES README.md DESTINATION share/doc/nirvana COMPONENT nirvana)

# --- paczka .deb (Devuan 6 "Excalibur" / Debian 13): cpack -G DEB ----------------
# Zależności od bibliotek liczy dpkg-shlibdeps; resztę (programy, z których
# Nirvana Desktop korzysta w czasie pracy) podajemy ręcznie. Recommends instaluje apt
# domyślnie; brakujące w repozytorium pakiety z Recommends apt po prostu pomija.
set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME nirvana)
set(CPACK_PACKAGE_VENDOR "Nirvana Desktop")
set(CPACK_PACKAGE_CONTACT "Nirvana Desktop <nirvana@localhost>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "lekkie środowisko graficzne w duchu Mac OS 9 i Amigi")
set(CPACK_PACKAGE_DESCRIPTION "Nirvana Desktop to sesja X11 zbudowana na FLTK: menedżer okien Insight, biurko i
menedżer plików Rambler, przełącznik aplikacji, panele sterowania (Alice)
i terminal Nirvana Shell. Interfejs po polsku i po angielsku.")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};NirvanaDesktop;nirvana;/")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION x11)
set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "dbus-bin | dbus")
# dawniej pakiet "lisaos" - apt usunie go przy instalacji (te same role, inne pliki)
set(CPACK_DEBIAN_PACKAGE_CONFLICTS "lisaos")
set(CPACK_DEBIAN_PACKAGE_REPLACES "lisaos")
set(CPACK_DEBIAN_PACKAGE_PROVIDES "lisaos")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS
  "xserver-xorg, xinit, fonts-dejavu-core, libpam-elogind | elogind, polkitd, pkexec, lxpolkit, \
udisks2, network-manager, pulseaudio-utils, alsa-utils, squashfs-tools, bubblewrap, xsecurelock, xdg-utils, \
xdg-desktop-portal, xdg-desktop-portal-gtk")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS
  "lightdm, network-manager-openvpn, network-manager-vpnc, wireguard-tools, power-profiles-daemon, pipewire-pulse")
include(CPack)
