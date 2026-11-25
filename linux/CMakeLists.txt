cmake_minimum_required(VERSION 3.16)
project(stone LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(OpenMP)
find_package(Threads REQUIRED)

add_executable(stone src/stone.cpp)
target_compile_options(stone PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>)
if(OpenMP_CXX_FOUND)
  target_link_libraries(stone PRIVATE OpenMP::OpenMP_CXX)
endif()

# Out-of-core strip solver and the picture tools that read its files (need libzstd).
find_library(ZSTD_LIB zstd)
find_path(ZSTD_INCLUDE zstd.h)
if(ZSTD_LIB AND ZSTD_INCLUDE)
  add_executable(stone_strips src/stone_strips.cpp)
  set(zstd_targets stone_strips)
  foreach(t star starlight height crop zoom)
    add_executable(stone_${t} tools/stone_${t}.cpp)
    list(APPEND zstd_targets stone_${t})
  endforeach()
  foreach(t ${zstd_targets})
    target_include_directories(${t} PRIVATE ${ZSTD_INCLUDE})
    target_link_libraries(${t} PRIVATE ${ZSTD_LIB} Threads::Threads)
    if(OpenMP_CXX_FOUND)
      target_link_libraries(${t} PRIVATE OpenMP::OpenMP_CXX)
    endif()
  endforeach()
endif()

# Picture tools that read a plain table.
foreach(t bulbs diag tri)
  add_executable(stone_${t} tools/stone_${t}.cpp)
  if(OpenMP_CXX_FOUND)
    target_link_libraries(stone_${t} PRIVATE OpenMP::OpenMP_CXX)
  endif()
endforeach()

# Optional GPU build (needs a CUDA toolkit that supports your GPU;
# RTX 50xx / sm_120 needs CUDA >= 12.8).
include(CheckLanguage)
check_language(CUDA)
if(CMAKE_CUDA_COMPILER)
  if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES native)
  endif()
  enable_language(CUDA)
  add_executable(stone_cuda src/stone_cuda.cu)
  set_target_properties(stone_cuda PROPERTIES CUDA_STANDARD 17)
else()
  message(STATUS "No CUDA compiler found; skipping stone_cuda")
endif()

add_executable(naive tests/naive.cpp)

add_executable(medial tools/medial.cpp)
if(OpenMP_CXX_FOUND)
  target_link_libraries(medial PRIVATE OpenMP::OpenMP_CXX)
endif()

enable_testing()
# The full 3000x3000 table must match the reference checksum.
add_test(NAME full_table_matches_reference
  COMMAND ${CMAKE_COMMAND}
    -DSTONE=$<TARGET_FILE:stone>
    -DREF_MD5=3a933e8f868aae300b0dd35c1d02f4ff
    -DOUT=${CMAKE_CURRENT_BINARY_DIR}/res_test.bin
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/compare.cmake)
# Independent check of the first 200x200 cells against the game rules.
add_test(NAME matches_game_rules
  COMMAND ${CMAKE_COMMAND}
    -DSTONE=$<TARGET_FILE:stone> -DNAIVE=$<TARGET_FILE:naive>
    -DOUT=${CMAKE_CURRENT_BINARY_DIR}/rules_test.bin
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/rules.cmake)
