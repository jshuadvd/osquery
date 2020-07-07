# Copyright (c) 2014-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed in accordance with the terms specified in
# the LICENSE file found in the root directory of this source tree.

# http://pkgs.fedoraproject.org/repo/pkgs/udev/udev-173.tar.bz2/91a88a359b60bbd074b024883cc0dbde/udev-173.tar.bz2

function(systemDMain)
	set(library_root "${CMAKE_CURRENT_SOURCE_DIR}/src")
	add_library(thirdparty_systemd INTERFACE)

	target_include_directories(thirdparty_systemd SYSTEM INTERFACE
    		"${library_root}/src"
  	)
	
	target_link_libraries(thirdparty_systemd INTERFACE
    		c_settings
  	)

endfunction()

systemDMain()
