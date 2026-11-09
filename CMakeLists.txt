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

# --- tłumaczenia (gettext): biblioteka i katalogi .mo z po/*.po ---------------
add_library(lisaos-i18n STATIC platinum/i18n.cxx)
target_include_directories(lisaos-i18n PUBLIC platinum)
find_program(MSGFMT msgfmt)
file(GLOB LISAOS_PO po/*.po)
set(LISAOS_MO)
if(MSGFMT)
  foreach(po ${LISAOS_PO})
    get_filename_component(lang ${po} NAME_WE)
    set(mo ${CMAKE_BINARY_DIR}/locale/${lang}/LC_MESSAGES/lisaos.mo)
    add_custom_command(OUTPUT ${mo}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/locale/${lang}/LC_MESSAGES
      COMMAND ${MSGFMT} --check -o ${mo} ${po}
      DEPENDS ${po} COMMENT "Tłumaczenie: ${lang}")
    list(APPEND LISAOS_MO ${mo})
    # share/lisaos/locale, nie share/locale: localepurge i "path-exclude" w
    # minimalnych instalacjach usuwają pliki .mo spoza wybranych języków
    install(FILES ${mo} DESTINATION share/lisaos/locale/${lang}/LC_MESSAGES COMPONENT lisaos)
  endforeach()
else()
  message(WARNING "Brak msgfmt (pakiet gettext) - interfejs będzie tylko po polsku")
endif()
add_custom_target(lisaos-translations ALL DEPENDS ${LISAOS_MO})

# --- libplatinum: wygląd i chrome okien wspólny dla wszystkich komponentów --
add_library(lisaos-platinum STATIC
  platinum/platinum.cxx
  platinum/pwindow.cxx
  platinum/anim.cxx
  platinum/theme.cxx
  platinum/wallpaper.cxx
  platinum/icons_wb.cxx
  platinum/framewin.cxx)
find_package(X11 REQUIRED)
target_include_directories(lisaos-platinum PUBLIC platinum)
target_link_libraries(lisaos-platinum PUBLIC lisaos-i18n fltk::fltk fltk::images X11::X11 X11::Xext)

# --- lisaos-net: sieć przez NetworkManager (nmcli + D-Bus) --------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(DBUS REQUIRED IMPORTED_TARGET dbus-1)
add_library(lisaos-net STATIC net/net.cxx)
target_include_directories(lisaos-net PUBLIC net)
target_link_libraries(lisaos-net PUBLIC lisaos-i18n fltk::fltk PkgConfig::DBUS)

# --- lisaos-audio i lisaos-power: aplety głośności i zasilania -------------------
pkg_check_modules(XSS REQUIRED IMPORTED_TARGET xscrnsaver)
add_library(lisaos-audio STATIC audio/audio.cxx)
target_include_directories(lisaos-audio PUBLIC audio)
target_link_libraries(lisaos-audio PUBLIC lisaos-i18n fltk::fltk)
add_library(lisaos-power STATIC power/power.cxx)
target_include_directories(lisaos-power PUBLIC power)
target_link_libraries(lisaos-power PUBLIC lisaos-i18n fltk::fltk PkgConfig::DBUS PkgConfig::XSS X11::Xext X11::X11)

# --- denise: menedżer plików ---------------------------------------------------------
add_executable(lisaos-denise
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
target_link_libraries(lisaos-denise PRIVATE lisaos-platinum lisaos-net lisaos-audio lisaos-power fltk::images PkgConfig::DBUS)

# --- insight: menedżer okien X11 ----------------------------------------------
add_executable(lisaos-insight insight/main.cxx)
target_link_libraries(lisaos-insight PRIVATE lisaos-platinum X11::X11)

# --- alice: panele sterowania (Wygląd) -------------------------------------------
add_executable(lisaos-alice alice/main.cxx alice/wyglad.cxx alice/siec.cxx alice/zasilanie.cxx alice/ustawienia.cxx)
target_link_libraries(lisaos-alice PRIVATE lisaos-platinum lisaos-net lisaos-power)

# --- Lisa Shell: emulator terminala (libvterm systemowy albo dołączony) --------
pkg_check_modules(VTERM IMPORTED_TARGET vterm>=0.3)
if(VTERM_FOUND)
  set(LISAOS_VTERM PkgConfig::VTERM)
else()
  file(GLOB VTERM_SRC third_party/libvterm/src/*.c)
  add_library(lisaos-vterm STATIC ${VTERM_SRC})
  set_target_properties(lisaos-vterm PROPERTIES C_STANDARD 99)
  target_include_directories(lisaos-vterm PUBLIC third_party/libvterm/include)
  set(LISAOS_VTERM lisaos-vterm)
endif()
add_executable(lisaos-shell shell/main.cxx)
target_link_libraries(lisaos-shell PRIVATE lisaos-platinum ${LISAOS_VTERM} util)

# Wszystko z komponentem "lisaos" - paczka .deb nie zabierze plików FLTK
# (FetchContent dokłada własne reguły install dla nagłówków i bibliotek).
install(TARGETS lisaos-denise lisaos-insight lisaos-alice lisaos-shell RUNTIME DESTINATION bin COMPONENT lisaos)
install(DIRECTORY schematy/ DESTINATION share/lisaos/schematy COMPONENT lisaos)
install(DIRECTORY tapety/ DESTINATION share/lisaos/tapety COMPONENT lisaos)
install(PROGRAMS session/lisaos-session session/lisaos-autostart DESTINATION bin COMPONENT lisaos)
install(FILES session/lisaos.desktop DESTINATION share/xsessions COMPONENT lisaos)
install(FILES README.md DESTINATION share/doc/lisaos COMPONENT lisaos)

# --- paczka .deb (Devuan 6 "Excalibur" / Debian 13): cpack -G DEB ----------------
# Zależności od bibliotek liczy dpkg-shlibdeps; resztę (programy, z których
# LisaOS korzysta w czasie pracy) podajemy ręcznie. Recommends instaluje apt
# domyślnie; brakujące w repozytorium pakiety z Recommends apt po prostu pomija.
set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME lisaos)
set(CPACK_PACKAGE_VENDOR "LisaOS")
set(CPACK_PACKAGE_CONTACT "LisaOS <lisaos@localhost>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "lekkie środowisko graficzne w duchu Mac OS 9 i Amigi")
set(CPACK_PACKAGE_DESCRIPTION "LisaOS to sesja X11 zbudowana na FLTK: menedżer okien Insight, biurko i
menedżer plików Rambler, przełącznik aplikacji, panele sterowania (Alice)
i terminal Lisa Shell. Interfejs po polsku i po angielsku.")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};LisaOS;lisaos;/")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION x11)
set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "dbus-bin | dbus")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS
  "xserver-xorg, xinit, fonts-dejavu-core, libpam-elogind | elogind, polkitd, pkexec, lxpolkit, \
udisks2, network-manager, pulseaudio-utils, alsa-utils, squashfs-tools, bubblewrap, xsecurelock, xdg-utils")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS
  "lightdm, network-manager-openvpn, network-manager-vpnc, wireguard-tools, power-profiles-daemon, pipewire-pulse")
include(CPack)
