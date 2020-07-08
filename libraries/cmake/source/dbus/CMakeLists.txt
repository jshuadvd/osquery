# Copyright (c) 2014-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed in accordance with the terms specified in
# the LICENSE file found in the root directory of this source tree.

function(dbusMain)
  # The first folder is to bypass the inclusion of CPack by shadowing
  # the inclusion of CPackInstallConfig
  list(APPEND CMAKE_MODULE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/modules"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cmake/modules"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cmake"
  )

  set(dbus_option_list
    DBUS_BUILD_TESTS
    DBUS_RELOCATABLE
    DBUS_ENABLE_PKGCONFIG
    DBUS_DISABLE_ASSERT
    DBUS_ENABLE_STATS
    DBUS_ENABLE_CONTAINERS
    DBUS_ENABLE_SYSTEMD
    ENABLE_USER_SESSION
    DBUS_WITH_SYSTEMD_SYSTEMUNITDIR
    DBUS_WITH_SYSTEMD_USERUNITDIR
    GLIB2_FOUND
    DBUS_WITH_GLIB
    DBUS_USE_OUTPUT_DEBUG_STRING
    DBUS_ENABLE_ANSI
    DBUS_ENABLE_VERBOSE_MODE
    DBUS_DISABLE_CHECKS
    DBUS_GCOV_ENABLED
    DBUS_BUS_ENABLE_INOTIFY
    HAVE_CONSOLE_OWNER_FILE
    X11_FOUND
    DBUS_BUILD_X11
    DBUS_INSTALL_SYSTEM_LIBS
  )

  foreach(dbus_option ${dbus_option_list})
    set("${dbus_option}" false CACHE BOOL "" FORCE)
  endforeach()

  set(EXPAT_FOUND true CACHE BOOL "" FORCE)
  set(EXPAT_INCLUDE_DIR "")

  add_subdirectory(src EXCLUDE_FROM_ALL)
  target_link_libraries(libdbus PRIVATE
    c_settings
  )

  # This does not work, the CMake they have written has too many hardcoded
  # paths and/or use the wrong variables (i.e.: CMAKE_SOURCE_DIR instead of CMAKE_CURRENT_SOURCE_DIR)
  add_library(thirdparty_dbus INTERFACE)
  target_link_libraries(thirdparty_dbus INTERFACE
    cxx_settings
    libdbus
  )
endfunction()

dbusMain()
