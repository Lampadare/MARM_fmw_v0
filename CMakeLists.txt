cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(NONE)

# CMakeLists.txt
include_directories(${CMAKE_SOURCE_DIR}/inc)

# NORDIC SDK APP START
target_sources(app PRIVATE
  src/main.c
  src/neuralbs.c
  src/fakedata_module.c
  src/fifo_buffer.c
  src/sd_card.c
)

# NORDIC SDK APP END
zephyr_library_include_directories(.)