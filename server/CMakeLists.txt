cmake_minimum_required(VERSION 3.20)
project(tinybbs_server LANGUAGES C CXX)

# The shared core builds against Linux Arduino look-alikes. HTTP/WebSocket and
# Telnet bindings are added in phases 3 and 4.
find_path(MBEDTLS_INCLUDE_DIR mbedtls/sha256.h REQUIRED)
find_library(MBEDCRYPTO_LIBRARY mbedcrypto REQUIRED)

add_library(tinybbs-miniz STATIC platform/miniz_tinfl.c)
target_include_directories(tinybbs-miniz PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/platform)

add_executable(tinybbs-phase1-smoke phase1_smoke.cpp)
target_compile_features(tinybbs-phase1-smoke PRIVATE cxx_std_17)
target_include_directories(tinybbs-phase1-smoke PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/platform
  ${CMAKE_CURRENT_SOURCE_DIR}/../src
  ${MBEDTLS_INCLUDE_DIR}
)
target_link_libraries(tinybbs-phase1-smoke PRIVATE tinybbs-miniz ${MBEDCRYPTO_LIBRARY})
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(tinybbs-phase1-smoke PRIVATE pthread)
endif()

add_executable(tinybbs-server main.cpp)
target_compile_features(tinybbs-server PRIVATE cxx_std_17)
target_include_directories(tinybbs-server PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/platform
  ${CMAKE_CURRENT_SOURCE_DIR}/../src
  ${MBEDTLS_INCLUDE_DIR}
)
target_link_libraries(tinybbs-server PRIVATE tinybbs-miniz ${MBEDCRYPTO_LIBRARY})
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(tinybbs-server PRIVATE pthread)
endif()
