cmake_minimum_required(VERSION 3.25)
project(StatWell VERSION 0.1.0 LANGUAGES CXX)

option(STATWELL_BUILD_TESTS "Build StatWell tests" ON)
set(STATWELL_GTEST_SOURCE_DIR "" CACHE PATH
  "GoogleTest source checkout to build with the selected compiler")
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(statwell_core STATIC src/metrics.cpp)
target_compile_features(statwell_core PUBLIC cxx_std_23)
set_target_properties(statwell_core PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(statwell_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_options(statwell_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(APPLE)
  target_sources(statwell_core PRIVATE src/platform/darwin.cpp)
  target_link_libraries(statwell_core PRIVATE
    "-framework CoreFoundation" "-framework IOKit")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_sources(statwell_core PRIVATE src/platform/linux_stub.cpp)
else()
  message(FATAL_ERROR "StatWell currently supports macOS and Linux only")
endif()

add_executable(statwell src/cli/main.cpp)
set_target_properties(statwell PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(statwell PRIVATE statwell_core)
target_compile_definitions(statwell PRIVATE STATWELL_VERSION="${PROJECT_VERSION}")
target_compile_options(statwell PRIVATE -Wall -Wextra -Wpedantic -Werror)
install(TARGETS statwell_core statwell EXPORT StatWellTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/statwell DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT StatWellTargets NAMESPACE StatWell::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/StatWell)
configure_package_config_file(cmake/StatWellConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/StatWellConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/StatWell)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/StatWellConfigVersion.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/StatWellConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/StatWellConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/StatWell)

if(STATWELL_BUILD_TESTS)
  include(CTest)
  if(STATWELL_GTEST_SOURCE_DIR)
    add_subdirectory(${STATWELL_GTEST_SOURCE_DIR}
      ${CMAKE_CURRENT_BINARY_DIR}/googletest EXCLUDE_FROM_ALL)
  else()
    find_package(GTest CONFIG REQUIRED)
  endif()
  add_executable(statwell_tests tests/metrics_test.cpp)
  set_target_properties(statwell_tests PROPERTIES CXX_EXTENSIONS OFF)
  target_link_libraries(statwell_tests PRIVATE statwell_core GTest::gtest_main)
  target_compile_options(statwell_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_test(NAME statwell_metrics COMMAND statwell_tests)
  add_test(NAME statwell_help COMMAND statwell --help)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME statwell_cli_contract
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_contract.py
      $<TARGET_FILE:statwell>)
endif()
