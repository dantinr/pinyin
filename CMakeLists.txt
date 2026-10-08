cmake_minimum_required(VERSION 3.20)
project(private_pinyin VERSION 0.1.0 LANGUAGES CXX)
if(NOT WIN32)
  message(FATAL_ERROR "This prototype currently targets Windows.")
endif()

add_library(pinyin_core src/lexicon.cpp src/user_store.cpp)
target_include_directories(pinyin_core PUBLIC include)
target_compile_features(pinyin_core PUBLIC cxx_std_17)
if(MSVC)
  target_compile_options(pinyin_core PUBLIC /utf-8 PRIVATE /W4 /permissive-)
else()
  target_compile_options(pinyin_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(private_pinyin src/main.cpp)
target_link_libraries(private_pinyin PRIVATE pinyin_core)
add_custom_command(TARGET private_pinyin POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:private_pinyin>/data"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/data/base.tsv"
    "$<TARGET_FILE_DIR:private_pinyin>/data/base.tsv")

include(CTest)
if(BUILD_TESTING)
  add_executable(pinyin_tests tests/core_tests.cpp)
  target_link_libraries(pinyin_tests PRIVATE pinyin_core)
  add_test(NAME pinyin_core COMMAND pinyin_tests "${CMAKE_CURRENT_SOURCE_DIR}/data/base.tsv")
  find_program(PWSH_EXECUTABLE NAMES pwsh)
  if(PWSH_EXECUTABLE)
    add_test(NAME pinyin_cli COMMAND "${PWSH_EXECUTABLE}" -NoProfile -File
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_tests.ps1"
      -Executable "$<TARGET_FILE:private_pinyin>")
  endif()
endif()
