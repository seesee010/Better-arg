cmake_minimum_required(VERSION 3.16)

project(better-arg
    VERSION 0.1.0
    DESCRIPTION "A C99 command line argument parsing library"
    HOMEPAGE_URL "https://github.com/seesee010/Better-arg"
    LANGUAGES C)

# C99, nothing newer, no compiler extensions.
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(ARG_TOP_LEVEL ON)
else()
    set(ARG_TOP_LEVEL OFF)
endif()

option(ARG_BUILD_TESTS "Build the unit tests" ${ARG_TOP_LEVEL})
option(ARG_BUILD_EXAMPLES "Build the examples" ${ARG_TOP_LEVEL})
option(ARG_WERROR "Treat warnings as errors" OFF)
option(ARG_SANITIZE "Build with AddressSanitizer and UBSan" OFF)
option(ARG_COVERAGE "Build with coverage instrumentation" OFF)
option(ARG_INSTALL "Generate install rules" ${ARG_TOP_LEVEL})

include(GNUInstallDirs)
include(cmake/ArgWarnings.cmake)

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------

file(GLOB ARG_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/*.c)

add_library(arg ${ARG_SOURCES})
add_library(BetterArg::arg ALIAS arg)
target_include_directories(arg
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src)
set_target_properties(arg PROPERTIES
    OUTPUT_NAME better-arg
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    C_VISIBILITY_PRESET default
    POSITION_INDEPENDENT_CODE ON)
arg_target_warnings(arg)

if(ARG_SANITIZE)
    set(ARG_SAN_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer
        -fno-sanitize-recover=all)
    target_compile_options(arg PUBLIC ${ARG_SAN_FLAGS})
    target_link_options(arg PUBLIC ${ARG_SAN_FLAGS})
endif()

if(ARG_COVERAGE)
    target_compile_options(arg PUBLIC --coverage -O0 -g)
    target_link_options(arg PUBLIC --coverage)
endif()

# ---------------------------------------------------------------------------
# Tests and examples
# ---------------------------------------------------------------------------

if(ARG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(ARG_BUILD_EXAMPLES AND EXISTS ${PROJECT_SOURCE_DIR}/examples/CMakeLists.txt)
    add_subdirectory(examples)
endif()

# ---------------------------------------------------------------------------
# Install: headers, library, CMake package and pkg-config file
# ---------------------------------------------------------------------------

if(ARG_INSTALL)
    include(CMakePackageConfigHelpers)

    install(TARGETS arg
        EXPORT BetterArgTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES include/arg.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(EXPORT BetterArgTargets
        NAMESPACE BetterArg::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/BetterArg)

    configure_package_config_file(cmake/BetterArgConfig.cmake.in
        ${PROJECT_BINARY_DIR}/BetterArgConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/BetterArg)
    write_basic_package_version_file(
        ${PROJECT_BINARY_DIR}/BetterArgConfigVersion.cmake
        COMPATIBILITY SameMinorVersion)
    install(FILES
        ${PROJECT_BINARY_DIR}/BetterArgConfig.cmake
        ${PROJECT_BINARY_DIR}/BetterArgConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/BetterArg)

    configure_file(cmake/better-arg.pc.in
        ${PROJECT_BINARY_DIR}/better-arg.pc @ONLY)
    install(FILES ${PROJECT_BINARY_DIR}/better-arg.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
endif()
