cmake_minimum_required(VERSION 3.14)

# ==========================================
# Option to build tests for Host (Local Machine)
# ==========================================
option(BUILD_HOST_TESTS "Build unit tests for the host machine (not for Pico)" OFF)

# ==========================================
# Setup Directories
# ==========================================
set(LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty")
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Source")
set(BIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Binaries")
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/CMake/Utils)

# ==========================================
# PICO SDK Setup (Skip if building host tests)
# ==========================================
if(NOT BUILD_HOST_TESTS)
    if(NOT DEFINED ENV{PICO_SDK_PATH})
        message(FATAL_ERROR "PICO_SDK_PATH environment variable not set")
    endif()
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
    include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
endif()

if (BUILD_HOST_TESTS)
    add_compile_definitions(UNIT_TEST)
endif()

project(pico_project C CXX ASM)

# ==========================================
# Compiler Settings
# ==========================================
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Initialize the SDK (Skip if building host tests)
if(NOT BUILD_HOST_TESTS)
    pico_sdk_init()
endif()

# ==========================================
# Source Globbing
# ==========================================
file(GLOB_RECURSE ALL_SOURCES 
    "${SRC_DIR}/*.cpp"
    "${SRC_DIR}/*.c"
    "${SRC_DIR}/*.h"
)

file(GLOB_RECURSE THIRD_PARTY_SOURCES 
    "${LIB_DIR}/*.cpp" 
    "${LIB_DIR}/*.c"
)

# ==========================================
# TARGET: PICO FIRMWARE (Default)
# ==========================================
if(NOT BUILD_HOST_TESTS)
    
    # Exclude test files from Pico build
    list(FILTER ALL_SOURCES EXCLUDE REGEX ".*\\.test\\.cpp$")
    
    add_executable(${PROJECT_NAME} ${ALL_SOURCES} ${THIRD_PARTY_SOURCES})

    target_include_directories(${PROJECT_NAME} PRIVATE ${SRC_DIR} ${LIB_DIR})

    target_link_libraries(${PROJECT_NAME}
        pico_stdlib pico_multicore pico_rand pico_stdio_usb
        hardware_spi hardware_pio hardware_gpio hardware_timer hardware_sync hardware_pwm
    )

    pico_enable_stdio_usb(${PROJECT_NAME} 1)
    pico_enable_stdio_uart(${PROJECT_NAME} 0)
    pico_add_extra_outputs(${PROJECT_NAME})

    set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${BIN_DIR}
    )

    target_compile_definitions(${PROJECT_NAME} PUBLIC
        USBD_DESC_STR_MAXLEN=64 USBD_PID=0x000a USBD_VID=0x2e8a
        USBD_MANUFACTURER="RTE" USBD_PRODUCT="RTE-800V300A"
    )

    add_custom_target(flash COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tools/Flash.sh DEPENDS ${PROJECT_NAME})
    add_custom_target(build-flash COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tools/Build_Flash.sh)

# ==========================================
# TARGET: HOST TESTS
# ==========================================
else()
    message(STATUS "Configuring for Host Test Build...")

    # Auto-download GoogleTest
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    # 1. Glob Test Sources
    file(GLOB_RECURSE TEST_SOURCES "${SRC_DIR}/*.test.cpp")

    # 2. Filter sources for Host Build
    # Remove Firmware Main (GTest provides main)
    list(FILTER ALL_SOURCES EXCLUDE REGEX ".*Main\\.cpp$")
    
    # Remove Test files from library list (linked separately)
    list(FILTER ALL_SOURCES EXCLUDE REGEX ".*\\.test\\.cpp$")

    # CRITICAL: Exclude Pico Hardware Specific Files
    # Add files here that include "hardware/xxx.h" or "pico/stdlib.h"
    list(FILTER ALL_SOURCES EXCLUDE REGEX ".*PWMDriver\\.cpp$")
    list(FILTER ALL_SOURCES EXCLUDE REGEX ".*svpwm\\.c$")
    list(FILTER ALL_SOURCES EXCLUDE REGEX ".*CommutationManager\\.cpp$") 
    list(FILTER ALL_SOURCES EXCLUDE REGEX ".*Telemetry\\.cpp$") 
    # Add other hardware dependent files above if the build fails on them

    # 3. Create Test Executable
    add_executable(${PROJECT_NAME}-Test 
        ${ALL_SOURCES} 
        ${THIRD_PARTY_SOURCES} 
        ${TEST_SOURCES}
    )

    target_include_directories(${PROJECT_NAME}-Test PRIVATE ${SRC_DIR} ${LIB_DIR})

    target_link_libraries(${PROJECT_NAME}-Test PRIVATE
        GTest::gtest
        GTest::gtest_main
        GTest::gmock
        GTest::gmock_main
    )

    # FIX: Include the GoogleTest module to enable gtest_discover_tests
    include(GoogleTest)

    gtest_discover_tests(${PROJECT_NAME}-Test
        WORKING_DIRECTORY ${BIN_DIR}
    )

    set_target_properties(${PROJECT_NAME}-Test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${BIN_DIR}
    )
endif()