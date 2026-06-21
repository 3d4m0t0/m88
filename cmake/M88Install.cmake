# FHS-style install rules for Linux packagers (Debian, Fedora, openSUSE, etc.).

include(GNUInstallDirs)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/linux/m88_port_version.h" _M88_PORT_VERSION_H)
string(REGEX MATCH "#define M88_PORT_VER_STRING \"([^\"]+)\"" _ "${_M88_PORT_VERSION_H}")
if(NOT CMAKE_MATCH_1)
  message(FATAL_ERROR "Could not parse M88 port version from src/linux/m88_port_version.h")
endif()
set(M88_PORT_VERSION "${CMAKE_MATCH_1}")
string(TIMESTAMP M88_RELEASE_DATE "%Y-%m-%d" UTC)

set(M88_QT_DOCDIR "${CMAKE_INSTALL_DATADIR}/doc/m88-qt")
set(M88_QT_LICENSEDIR "${CMAKE_INSTALL_DATADIR}/licenses/m88-qt")

if(TARGET m88-qt)
  set(M88_QT_DOCDIR_FULL "${CMAKE_INSTALL_PREFIX}/${M88_QT_DOCDIR}")

  # Desktop Entry / AppStream must be UTF-8 (Shift_JIS breaks KDE launcher text).
  foreach(_M88_UTF8_FILE
      "${CMAKE_CURRENT_SOURCE_DIR}/data/m88-qt.desktop.in"
      "${CMAKE_CURRENT_SOURCE_DIR}/data/m88-qt.metainfo.xml.in")
    file(READ "${_M88_UTF8_FILE}" _M88_UTF8_CONTENT UTF-8)
  endforeach()
  unset(_M88_UTF8_FILE)
  unset(_M88_UTF8_CONTENT)

  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/data/m88-qt.desktop.in"
    "${CMAKE_CURRENT_BINARY_DIR}/m88-qt.desktop"
    @ONLY)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/data/m88-qt.metainfo.xml.in"
    "${CMAKE_CURRENT_BINARY_DIR}/m88-qt.metainfo.xml"
    @ONLY)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/data/man/m88-qt.1.in"
    "${CMAKE_CURRENT_BINARY_DIR}/m88-qt.1"
    @ONLY)

  install(TARGETS m88-qt
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/m88-qt.desktop"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/m88-qt.metainfo.xml"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)

  install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/icons/hicolor"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons
    USE_SOURCE_PERMISSIONS
    FILES_MATCHING PATTERN "*.png")

  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/m88-qt.1"
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

  install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/README_WINDOWS.md"
    DESTINATION ${M88_QT_DOCDIR})

  install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/data/LICENSE.BSD"
    DESTINATION ${M88_QT_LICENSEDIR})

  file(GLOB M88_EXTERNAL_TRANSLATIONS
    "${CMAKE_CURRENT_SOURCE_DIR}/translations/m88-qt_*.json")
  list(FILTER M88_EXTERNAL_TRANSLATIONS EXCLUDE REGEX ".*/m88-qt_(en|ja)\\.json$")
  if(M88_EXTERNAL_TRANSLATIONS)
    install(FILES ${M88_EXTERNAL_TRANSLATIONS}
      DESTINATION ${CMAKE_INSTALL_DATADIR}/m88-qt/translations)
  endif()

  message(STATUS "Install rules: ${CMAKE_INSTALL_BINDIR}/m88-qt, "
                 "${CMAKE_INSTALL_DATADIR}/applications/m88-qt.desktop, "
                 "${M88_QT_DOCDIR}/")
endif()

if(TARGET m88)
  install(TARGETS m88
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  message(STATUS "Install rules: ${CMAKE_INSTALL_BINDIR}/m88 (SDL2 frontend)")
endif()

unset(_M88_PORT_VERSION_H)
