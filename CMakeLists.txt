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
  platinum/filedialog.cxx
  platinum/pwindow.cxx
  platinum/anim.cxx
  platinum/theme.cxx
  platinum/wallpaper.cxx
  platinum/clockconf.cxx
  platinum/icons_retro.cxx
  platinum/icons3d.cxx
  platinum/framewin.cxx)
find_package(X11 REQUIRED)
target_include_directories(nirvana-platinum PUBLIC platinum)
target_link_libraries(nirvana-platinum PUBLIC nirvana-i18n fltk::fltk fltk::images X11::X11 X11::Xext X11::Xcursor)

# --- nirvana-net: networking via NetworkManager (nmcli + D-Bus) -------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(DBUS REQUIRED IMPORTED_TARGET dbus-1)
add_library(nirvana-net STATIC net/net.cxx)
target_include_directories(nirvana-net PUBLIC net)
target_link_libraries(nirvana-net PUBLIC nirvana-i18n fltk::fltk PkgConfig::DBUS)

# --- nirvana-bt: Bluetooth via BlueZ (bluetoothctl) --------------------------
add_library(nirvana-bt STATIC bt/bt.cxx)
target_include_directories(nirvana-bt PUBLIC bt)
target_link_libraries(nirvana-bt PUBLIC nirvana-i18n fltk::fltk util)

# --- NirvanaSHIELD: the firewall service and its client library --------------
add_library(nirvana-shield-client STATIC shield/client.cxx)
target_include_directories(nirvana-shield-client PUBLIC shield)
add_executable(nirvana-shield shield/daemon.cxx)
target_link_libraries(nirvana-shield PRIVATE nirvana-shield-client)
install(TARGETS nirvana-shield RUNTIME DESTINATION sbin COMPONENT nirvana)
install(PROGRAMS shield/nirvana-shield.init DESTINATION /etc/init.d RENAME nirvana-shield COMPONENT nirvana)
install(FILES shield/nirvana-shield.service DESTINATION lib/systemd/system COMPONENT nirvana)

# --- nirvana-audio and nirvana-power: volume and power applets ---------------
pkg_check_modules(XSS REQUIRED IMPORTED_TARGET xscrnsaver)
add_library(nirvana-audio STATIC audio/audio.cxx)
target_include_directories(nirvana-audio PUBLIC audio)
target_link_libraries(nirvana-audio PUBLIC nirvana-i18n fltk::fltk)
# --- printing (CUPS) - optional: without libcups there is no printer applet and panel ---
# (pkg-config first; when cups.pc lacks its own dependencies, the header and the library)
pkg_check_modules(CUPS IMPORTED_TARGET cups)
if(CUPS_FOUND)
  set(NIRVANA_CUPS PkgConfig::CUPS)
else()
  find_path(CUPS_HEADER cups/cups.h)
  find_library(CUPS_LIBRARY cups)
  if(CUPS_HEADER AND CUPS_LIBRARY)
    set(CUPS_FOUND TRUE)
    set(NIRVANA_CUPS ${CUPS_LIBRARY})
  endif()
endif()
if(CUPS_FOUND)
  add_library(nirvana-print STATIC print/print.cxx)
  target_include_directories(nirvana-print PUBLIC print)
  target_link_libraries(nirvana-print PUBLIC nirvana-i18n ${NIRVANA_CUPS})
  target_compile_definitions(nirvana-print PUBLIC NIRVANA_PRINT=1)
  set(NIRVANA_PRINT_LIB nirvana-print)
else()
  message(WARNING "libcups development files not found - no printer applet (Devuan: libcups2-dev)")
  set(NIRVANA_PRINT_LIB "")
endif()

add_library(nirvana-power STATIC power/power.cxx)
target_include_directories(nirvana-power PUBLIC power)
target_link_libraries(nirvana-power PUBLIC nirvana-i18n fltk::fltk PkgConfig::DBUS PkgConfig::XSS X11::Xext X11::X11)

find_package(Threads REQUIRED)

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
  denise/updates.cxx
  denise/clock.cxx
  denise/printer.cxx
  denise/about.cxx
  denise/tray.cxx
  denise/bluetooth.cxx
  denise/clipboard.cxx
  denise/notify.cxx
  denise/shield_applet.cxx
  denise/explorer.cxx
  denise/filetypes.cxx
  denise/jobs.cxx denise/sounds.cxx denise/openwith.cxx
  denise/sniff.cxx
  denise/metadata.cxx
  denise/search.cxx)
target_link_libraries(nirvana-denise PRIVATE Threads::Threads nirvana-platinum nirvana-net nirvana-bt nirvana-shield-client nirvana-audio nirvana-power ${NIRVANA_PRINT_LIB} fltk::images PkgConfig::DBUS)

# --- insight: X11 window manager ---------------------------------------------
add_executable(nirvana-insight insight/main.cxx)
target_link_libraries(nirvana-insight PRIVATE nirvana-platinum X11::X11 X11::Xfixes)

# --- alice: control panels ---------------------------------------------------
add_executable(nirvana-alice alice/main.cxx alice/appearance.cxx alice/network.cxx alice/power.cxx alice/preferences.cxx alice/shader.cxx alice/datetime.cxx alice/desktop.cxx alice/printers.cxx alice/user.cxx alice/saver.cxx alice/bluetooth.cxx alice/shield.cxx)
target_link_libraries(nirvana-alice PRIVATE nirvana-platinum nirvana-net nirvana-bt nirvana-shield-client nirvana-power ${NIRVANA_PRINT_LIB})

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

# --- nirvana-shader: full-screen GLSL effects (a small compositor) -------------
pkg_check_modules(SHADERDEPS IMPORTED_TARGET gl xcomposite xdamage xfixes xrender xext)
if(SHADERDEPS_FOUND)
  add_executable(nirvana-shader shader/nirvana-shader.cxx)
  target_link_libraries(nirvana-shader PRIVATE PkgConfig::SHADERDEPS X11::X11)
  install(TARGETS nirvana-shader RUNTIME DESTINATION bin COMPONENT nirvana)
else()
  message(WARNING "OpenGL/XComposite/XDamage development files not found - nirvana-shader is not built "
                  "(Devuan: libgl-dev libxcomposite-dev libxdamage-dev libxfixes-dev libxrender-dev)")
endif()
install(DIRECTORY shaders/ DESTINATION share/nirvana/shaders COMPONENT nirvana)
# system updates: the check (as the user) and the helper (root, through pkexec/polkit)
install(PROGRAMS update/nirvana-update-check update/nirvana-update-helper update/nirvana-power-helper update/nirvana-time-helper update/nirvana-print-helper update/nirvana-user-helper update/nirvana-libs-helper update/nirvana-pkexec update/nirvana-ramdisk-helper DESTINATION libexec COMPONENT nirvana)
install(FILES update/org.nirvana-desktop.policy DESTINATION share/polkit-1/actions COMPONENT nirvana)
install(FILES update/50-nirvana.rules DESTINATION share/polkit-1/rules.d COMPONENT nirvana)

# --- Nirvana-DM: the login screen (a LightDM greeter) - optional --------------
pkg_check_modules(LIGHTDM IMPORTED_TARGET liblightdm-gobject-1 gobject-2.0 glib-2.0)
if(LIGHTDM_FOUND)
  add_executable(nirvana-dm-greeter dm/nirvana-dm-greeter.cxx)
  target_link_libraries(nirvana-dm-greeter PRIVATE nirvana-platinum fltk::images PkgConfig::LIGHTDM)
  install(TARGETS nirvana-dm-greeter RUNTIME DESTINATION sbin COMPONENT nirvana)
  install(FILES dm/nirvana-dm.desktop DESTINATION share/xgreeters COMPONENT nirvana)
  install(FILES dm/50-nirvana-dm.conf DESTINATION share/lightdm/lightdm.conf.d COMPONENT nirvana)
else()
  message(WARNING "liblightdm-gobject not found - Nirvana-DM is not built (Devuan: liblightdm-gobject-dev)")
endif()

# --- nirvana-portal: the Nirvana file dialog for other programs (xdg-desktop-portal) --------
pkg_check_modules(GIO IMPORTED_TARGET gio-2.0)
if(GIO_FOUND)
  add_executable(nirvana-portal portal/nirvana-portal.cxx)
  target_link_libraries(nirvana-portal PRIVATE nirvana-platinum fltk::images PkgConfig::GIO)
  install(TARGETS nirvana-portal RUNTIME DESTINATION libexec COMPONENT nirvana)
  configure_file(portal/org.freedesktop.impl.portal.desktop.nirvana.service.in
                 org.freedesktop.impl.portal.desktop.nirvana.service @ONLY)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.impl.portal.desktop.nirvana.service
          DESTINATION share/dbus-1/services COMPONENT nirvana)
  install(FILES portal/nirvana.portal DESTINATION share/xdg-desktop-portal/portals COMPONENT nirvana)
else()
  message(WARNING "gio-2.0 not found - nirvana-portal is not built (Devuan: libglib2.0-dev)")
endif()

# --- nirvana-polkit-agent: the password window of polkit ----------------------------------
pkg_check_modules(POLKITAGENT IMPORTED_TARGET polkit-agent-1)
if(POLKITAGENT_FOUND)
  add_executable(nirvana-polkit-agent polkit/nirvana-polkit-agent.cxx)
  target_link_libraries(nirvana-polkit-agent PRIVATE nirvana-platinum PkgConfig::POLKITAGENT)
  install(TARGETS nirvana-polkit-agent RUNTIME DESTINATION libexec COMPONENT nirvana)
else()
  message(WARNING "polkit-agent-1 not found - nirvana-polkit-agent is not built (Devuan: libpolkit-agent-1-dev)")
endif()

# --- the screen savers and nirvana-lock (screen saver + lock, PAM) ------------------------
add_library(nirvana-savers STATIC savers/savers.cxx)
target_include_directories(nirvana-savers PUBLIC savers)
target_link_libraries(nirvana-savers PUBLIC nirvana-platinum)
target_link_libraries(nirvana-alice PRIVATE nirvana-savers)
find_path(PAM_INCLUDE_DIR security/pam_appl.h)
find_library(PAM_LIBRARY pam)
if(PAM_INCLUDE_DIR AND PAM_LIBRARY)
  add_executable(nirvana-lock lock/nirvana-lock.cxx)
  target_include_directories(nirvana-lock PRIVATE ${PAM_INCLUDE_DIR})
  target_link_libraries(nirvana-lock PRIVATE nirvana-savers nirvana-power nirvana-platinum fltk::images ${PAM_LIBRARY})
  install(TARGETS nirvana-lock RUNTIME DESTINATION bin COMPONENT nirvana)
  install(FILES lock/nirvana-lock.pam DESTINATION /etc/pam.d RENAME nirvana-lock COMPONENT nirvana)
else()
  message(WARNING "PAM not found - nirvana-lock is not built (Devuan: libpam0g-dev)")
endif()

# tools: the logo sheet (cmake --build . --target logo-sheet), not installed
add_executable(logo-sheet EXCLUDE_FROM_ALL tools/logo-sheet.cxx)
target_link_libraries(logo-sheet PRIVATE nirvana-platinum)

# Everything in the "nirvana" component - the .deb package does not take FLTK files
# (FetchContent adds its own install rules for headers and libraries).
install(TARGETS nirvana-denise nirvana-insight nirvana-alice nirvana-shell RUNTIME DESTINATION bin COMPONENT nirvana)
install(DIRECTORY schemes/ DESTINATION share/nirvana/schemes COMPONENT nirvana)
install(DIRECTORY wallpapers/ DESTINATION share/nirvana/wallpapers COMPONENT nirvana)
install(DIRECTORY cursors/ DESTINATION share/icons COMPONENT nirvana PATTERN "preview.png" EXCLUDE)   # the red Retro 16 pointer themes
install(PROGRAMS session/nirvana-session session/nirvana-autostart DESTINATION bin COMPONENT nirvana)
install(FILES session/nirvana.desktop DESTINATION share/xsessions COMPONENT nirvana)
install(FILES session/nirvana-portals.conf DESTINATION share/xdg-desktop-portal COMPONENT nirvana)
install(FILES README.md README.pl.md DESTINATION share/doc/nirvana COMPONENT nirvana)
install(FILES LICENSE DESTINATION share/nirvana COMPONENT nirvana)   # shown in About This Computer

# --- .deb package (Devuan 6 "Excalibur" / Debian 13): cpack -G DEB ----------
# Library dependencies come from dpkg-shlibdeps; the rest (programs Nirvana Desktop
# uses at run time) is listed by hand. apt installs Recommends by default and
# simply skips Recommends that are missing from the repositories.
# The libxcb-* / libxkbcommon-x11 libraries: Qt 6 programs in AppImages count on them.
set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME nirvana)
set(CPACK_PACKAGE_VENDOR "Nirvana Desktop")
set(CPACK_PACKAGE_CONTACT "Nirvana Desktop <nirvana@localhost>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "lightweight desktop in the spirit of the classic 1990s desktops")
set(CPACK_PACKAGE_DESCRIPTION "Nirvana Desktop is an X11 session built with FLTK: the Insight window manager,
the Rambler desktop and file manager, an application switcher, control panels (Alice)
and the Nirvana Shell terminal. English and Polish interface.")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};NirvanaDesktop;nirvana;/")
set(CPACK_DEBIAN_PACKAGE_VERSION "${NIRVANA_VERSION}")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION x11)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "dbus-bin | dbus")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS
  "xserver-xorg, xinit, fonts-dejavu-core, libpam-elogind | elogind, polkitd, pkexec, \
udisks2, network-manager, bluez, rfkill, nftables, libarchive-tools, archivemount, sshfs, pulseaudio-utils, alsa-utils, squashfs-tools, bubblewrap, xdg-utils, \
xdg-desktop-portal, xdg-desktop-portal-gtk, libgl1-mesa-dri, \
libxcb-cursor0, libxcb-icccm4, libxcb-keysyms1, libxcb-image0, libxcb-render-util0, libxkbcommon-x11-0, \
libxcb-xkb1, libxcb-shape0, libxcb-randr0, libxcb-xinerama0")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS
  "lightdm, network-manager-openvpn, network-manager-vpnc, wireguard-tools, power-profiles-daemon, pipewire-pulse, cups, gvfs-backends, gvfs-fuse, ffmpeg")
include(CPack)
