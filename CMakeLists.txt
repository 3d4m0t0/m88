cmake_minimum_required(VERSION 3.20)

# openSUSE suse-devel-config sets CXX/CC to "ccache g++" / "ccache gcc".
# CMake treats the whole string as the compiler path, which breaks Qt AutoMoc.
foreach(_M88_COMPILER_VAR CC CXX)
  if(DEFINED ENV{${_M88_COMPILER_VAR}})
    string(STRIP "$ENV{${_M88_COMPILER_VAR}}" _M88_COMPILER_VALUE)
    if(_M88_COMPILER_VALUE MATCHES "^ccache[ ]+(.+)$")
      set(ENV{${_M88_COMPILER_VAR}} "${CMAKE_MATCH_1}")
    endif()
    unset(_M88_COMPILER_VALUE)
  endif()
endforeach()
unset(_M88_COMPILER_VAR)

project(M88 LANGUAGES C CXX)

find_program(M88_CCACHE_PROGRAM ccache)
if(M88_CCACHE_PROGRAM)
  set(CMAKE_C_COMPILER_LAUNCHER "${M88_CCACHE_PROGRAM}" CACHE STRING "" FORCE)
  set(CMAKE_CXX_COMPILER_LAUNCHER "${M88_CCACHE_PROGRAM}" CACHE STRING "" FORCE)
endif()
unset(M88_CCACHE_PROGRAM)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(M88_BUILD_CORE_ONLY "Build portable emulation core library" ON)
option(M88_BUILD_QT_FRONTEND "Build Qt6 frontend executable (m88-qt)" ON)
option(M88_BUILD_LINUX_FRONTEND "Build experimental SDL2 frontend (m88); incomplete" OFF)
option(M88_NATIVE_ARCH "Tune for host CPU (-march=native, GCC/Clang only)" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

set(M88_CORE_INCLUDE_DIRS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/linux_compat
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/common
  ${CMAKE_CURRENT_SOURCE_DIR}/src/devices
  ${CMAKE_CURRENT_SOURCE_DIR}/src/if
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pc88
)

file(GLOB M88_COMMON_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/common/*.cpp"
)
file(GLOB M88_PC88_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/pc88/*.cpp"
)
file(GLOB M88_DEVICE_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/devices/*.cpp"
)
file(GLOB M88_ZLIB_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/zlib/*.c"
)

# MSVC inline-assembly implementation; not buildable on Linux compilers.
list(REMOVE_ITEM M88_DEVICE_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/src/devices/Z80Debug.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/devices/Z80Test.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/devices/Z80_x86.cpp"
)

add_library(m88_core STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/linux_compat/diag.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/linux_compat/file.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/linux_compat/loadmon.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/linux_compat/path.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/linux_compat/rom_log.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/linux_compat/status.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/linux/linux_startup_log.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/linux/linux_paths.cpp
  ${M88_COMMON_SOURCES}
  ${M88_PC88_SOURCES}
  ${M88_DEVICE_SOURCES}
  ${M88_ZLIB_SOURCES}
)

target_include_directories(m88_core PUBLIC
  ${M88_CORE_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}/src/linux
)

target_compile_definitions(m88_core PUBLIC
  M88_LINUX_PORT=1
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(m88_core PRIVATE
    -fpermissive
    -Wall
    -Wextra
  )
  if(M88_NATIVE_ARCH)
    target_compile_options(m88_core PRIVATE -march=native)
  endif()
endif()

message(STATUS "M88 build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Configured m88_core (Linux core library)")

if(M88_BUILD_QT_FRONTEND)
  find_package(Qt6 QUIET COMPONENTS Widgets)
  if(NOT Qt6_FOUND)
    message(WARNING "Qt6 not found; m88-qt will not be built (install Qt6 Widgets development package)")
  else()
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)

      add_executable(m88-qt
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/main_qt.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/main_window.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/main_window.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/config_dialog.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/config_dialog.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/about_dialog.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/about_dialog.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/confirm_dialog.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/confirm_dialog.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/m88_i18n.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/m88_i18n.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/m88_i18n.qrc
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/m88_icons.qrc
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/multi_disk_editor_dialog.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/multi_disk_editor_dialog.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/emu_view.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/emu_view.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/emulator_controller.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/emulator_controller.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/qt_input.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/qt_input.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/qt_host_input.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/qt_host_input.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/qt_video_log.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/qt_platform.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/qt_platform.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/screen_capture.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/screen_capture.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/linux/keyboard_vk.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/linux/shared_framebuffer_draw.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/linux/linux_emu_thread.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/linux/linux_config.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/linux/linux_ime.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/linux/m88_wayland_idle_inhibit.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/linux/m88_wayland_idle_inhibit.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/linux/half_kana_ime.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/linux/pc88_key_fixup.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/linux/qt_miniaudio_sound.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/linux/m88_miniaudio_devices.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/linux/pc88_keyboard.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/win32/WinKeyIF.cpp
      )

      target_include_directories(m88-qt PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt
        ${CMAKE_CURRENT_SOURCE_DIR}/src/linux
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio
        ${M88_CORE_INCLUDE_DIRS}
      )

      target_link_libraries(m88-qt PRIVATE m88_core Qt6::Widgets pthread dl m)

      find_package(PkgConfig QUIET)
      if(PkgConfig_FOUND)
        pkg_check_modules(M88_WAYLAND_CLIENT QUIET wayland-client)
      endif()
      find_program(M88_WAYLAND_SCANNER wayland-scanner)
      set(M88_IDLE_INHIBIT_XML
          "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wayland-protocols/idle-inhibit-unstable-v1.xml")
      if(M88_WAYLAND_CLIENT_FOUND AND M88_WAYLAND_SCANNER AND EXISTS "${M88_IDLE_INHIBIT_XML}")
        set(M88_IDLE_INHIBIT_CLIENT_H
            "${CMAKE_CURRENT_BINARY_DIR}/idle-inhibit-unstable-v1-client-protocol.h")
        set(M88_IDLE_INHIBIT_PRIVATE_C
            "${CMAKE_CURRENT_BINARY_DIR}/idle-inhibit-unstable-v1-private-code.c")
        add_custom_command(
          OUTPUT "${M88_IDLE_INHIBIT_CLIENT_H}"
          COMMAND "${M88_WAYLAND_SCANNER}" client-header
                  "${M88_IDLE_INHIBIT_XML}" "${M88_IDLE_INHIBIT_CLIENT_H}"
          DEPENDS "${M88_IDLE_INHIBIT_XML}"
          VERBATIM)
        add_custom_command(
          OUTPUT "${M88_IDLE_INHIBIT_PRIVATE_C}"
          COMMAND "${M88_WAYLAND_SCANNER}" private-code
                  "${M88_IDLE_INHIBIT_XML}" "${M88_IDLE_INHIBIT_PRIVATE_C}"
          DEPENDS "${M88_IDLE_INHIBIT_XML}"
          VERBATIM)
        add_custom_target(m88_idle_inhibit_codegen
          DEPENDS "${M88_IDLE_INHIBIT_CLIENT_H}" "${M88_IDLE_INHIBIT_PRIVATE_C}")
        add_dependencies(m88-qt m88_idle_inhibit_codegen)
        target_sources(m88-qt PRIVATE "${M88_IDLE_INHIBIT_PRIVATE_C}")
        target_include_directories(m88-qt PRIVATE
          "${CMAKE_CURRENT_BINARY_DIR}"
          ${M88_WAYLAND_CLIENT_INCLUDE_DIRS})
        target_link_libraries(m88-qt PRIVATE ${M88_WAYLAND_CLIENT_LIBRARIES})
        target_compile_definitions(m88-qt PRIVATE M88_HAS_WAYLAND_IDLE_INHIBIT=1)
        message(STATUS "m88-qt: Wayland idle-inhibit enabled")
      else()
        message(STATUS "m88-qt: Wayland idle-inhibit disabled (wayland-client or wayland-scanner missing)")
      endif()

      target_compile_definitions(m88-qt PRIVATE
        M88_LINUX_PORT=1
        M88_QT_FRONTEND=1
      )

      if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(m88-qt PRIVATE -fpermissive)
        if(M88_NATIVE_ARCH)
          target_compile_options(m88-qt PRIVATE -march=native)
        endif()
      endif()

      message(STATUS "Configured m88-qt (Qt6 frontend, miniaudio)")
  endif()
endif()

if(M88_BUILD_LINUX_FRONTEND)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(SDL2 IMPORTED_TARGET sdl2)
  endif()

  if(NOT SDL2_FOUND)
    message(WARNING "SDL2 not found; m88 (SDL2 frontend) will not be built (install libSDL2 development package)")
  else()
    add_executable(m88
      ${CMAKE_CURRENT_SOURCE_DIR}/src/linux/main.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/linux/linux_draw.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/linux/linux_config.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/linux/linux_input.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/linux/keyboard_vk.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/linux/keyboard_vk_sdl.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/linux/linux_sound.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/linux/linux_ime.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/linux/half_kana_ime.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/linux/pc88_key_fixup.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/linux/pc88_keyboard.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/win32/WinKeyIF.cpp
    )

    target_include_directories(m88 PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src/linux
      ${M88_CORE_INCLUDE_DIRS}
    )

    target_link_libraries(m88 PRIVATE m88_core PkgConfig::SDL2)

    target_compile_definitions(m88 PRIVATE M88_LINUX_PORT=1)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(m88 PRIVATE -fpermissive)
      if(M88_NATIVE_ARCH)
        target_compile_options(m88 PRIVATE -march=native)
      endif()
    endif()

    message(STATUS "Configured m88 (experimental SDL2 frontend)")
  endif()
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/M88Install.cmake")
