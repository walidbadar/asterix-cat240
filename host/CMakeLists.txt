# SPDX-FileCopyrightText: 2026 Muhammad Waleed Badar
# SPDX-License-Identifier: GPL-3.0-only
#
# Builds the library, the cat240-send tool and the test suites for Linux or
# any other POSIX system:
#
#   cmake -S host -B build-host && cmake --build build-host && ctest --test-dir build-host

cmake_minimum_required(VERSION 3.20)
project(asterix_cat240 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)

set(CAT240_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(CAT240_LIB ${CAT240_ROOT}/lib/asterix_cat240)

option(ASTERIX_CAT240_BUILD_TESTS "Build the test suites" ON)

add_library(asterix_cat240 STATIC
  ${CAT240_LIB}/cat240.c
  ${CAT240_LIB}/cat240_codec.c
  ${CAT240_LIB}/cat240_cartesian.c
)
target_include_directories(asterix_cat240 PUBLIC ${CAT240_ROOT}/include)
target_compile_options(asterix_cat240 PRIVATE -Wall -Wextra)
target_link_libraries(asterix_cat240 PUBLIC m)

add_executable(cat240-send cat240_send.c)
target_compile_options(cat240-send PRIVATE -Wall -Wextra)
target_link_libraries(cat240-send PRIVATE asterix_cat240)

install(TARGETS asterix_cat240 cat240-send)
install(DIRECTORY ${CAT240_ROOT}/include/asterix TYPE INCLUDE)

if(ASTERIX_CAT240_BUILD_TESTS)
  enable_testing()

  add_library(ztest_shim STATIC shim/ztest_main.c)
  target_include_directories(ztest_shim PUBLIC shim)

  # The library built without the time of day, as the second Zephyr scenario does.
  add_library(asterix_cat240_no_tod STATIC
    ${CAT240_LIB}/cat240.c
    ${CAT240_LIB}/cat240_codec.c
    ${CAT240_LIB}/cat240_cartesian.c
  )
  target_include_directories(asterix_cat240_no_tod PUBLIC ${CAT240_ROOT}/include)
  target_compile_definitions(asterix_cat240_no_tod PUBLIC ASTERIX_CAT240_TIME_OF_DAY=0)
  target_link_libraries(asterix_cat240_no_tod PUBLIC m)

  add_executable(cat240_codec_test ${CAT240_ROOT}/tests/lib/cat240_codec/src/main.c)
  target_include_directories(cat240_codec_test PRIVATE ${CAT240_LIB})
  target_link_libraries(cat240_codec_test PRIVATE asterix_cat240 ztest_shim)
  add_test(NAME lib.cat240_codec COMMAND cat240_codec_test)

  add_executable(asterix_cat240_test ${CAT240_ROOT}/tests/lib/asterix_cat240/src/main.c)
  target_include_directories(asterix_cat240_test PRIVATE ${CAT240_LIB})
  target_link_libraries(asterix_cat240_test PRIVATE asterix_cat240 ztest_shim)
  add_test(NAME lib.asterix_cat240 COMMAND asterix_cat240_test)

  add_executable(asterix_cat240_no_tod_test ${CAT240_ROOT}/tests/lib/asterix_cat240/src/main.c)
  target_include_directories(asterix_cat240_no_tod_test PRIVATE ${CAT240_LIB})
  target_link_libraries(asterix_cat240_no_tod_test PRIVATE asterix_cat240_no_tod ztest_shim)
  add_test(NAME lib.asterix_cat240.no_time_of_day COMMAND asterix_cat240_no_tod_test)
endif()
