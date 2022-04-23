#
# CMakeLists.txt
#
# Copyright (C) 2010 - 2022 Alfred E. Heggestad
#

##############################################################################
#
# Versioning
#

cmake_minimum_required(VERSION 3.7)

project(baresip-webrtc)


##############################################################################
#
# Module/Package Includes
#


##############################################################################
#
# Compile options
#

set(CMAKE_C_STANDARD 11)

include_directories(
  src
  /usr/local/include
  /usr/local/include/re
  /usr/local/include/rem
)

link_directories(/usr/local/lib)


##############################################################################
#
# Source/Header section
#

add_executable(${PROJECT_NAME}
  src/demo.c
  src/main.c
  src/mediatrack.c
  src/peerconn.c
  src/http.c
  src/fs.c
)

target_link_libraries(${PROJECT_NAME} -lbaresip -lrem -lre -lz -lpthread)
