cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME STM32CubeMX)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM, C and C++ languages
enable_language(C CXX ASM)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Src/Inverter/InverterMain.cpp
    Src/Inverter/Telemetry.cpp
    Src/Inverter/Control/OpenLoopController.cpp
    Src/Inverter/Control/CommandShell.cpp
    Src/Inverter/Control/FaultManager.cpp
    Src/Inverter/Control/FaultCallbacks.cpp
    Src/Inverter/Calibration/PolePairCalibrator.cpp
    Src/Inverter/Drivers/GateDriver/gate_driver.c
    Src/Inverter/Drivers/CAN/can_display.c
    Src/Inverter/Drivers/UART/mcp2221a_driver.c
    Src/Inverter/Drivers/Storage/cy15b102q_driver.c
    Src/Inverter/Drivers/Logging/ontime_logger.c
    Src/Inverter/Drivers/PWM/pwm.c
    Src/Inverter/Drivers/Sensors/CurrentSensor.cpp
    Src/Inverter/Drivers/Sensors/CurrentSensorTest.cpp
    Src/Inverter/Drivers/Sensors/DcLinkVoltageSensor.cpp
    Src/Inverter/Drivers/Sensors/EncoderADC.cpp
    Src/Inverter/Drivers/Sensors/PhaseCurrentADC.cpp
    Src/Inverter/Drivers/Sensors/PolePairEstimator.cpp
    Src/Inverter/Drivers/Sensors/MAX22530.cpp
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Inc/Inverter
    Inc/Inverter/Control
    Inc/Inverter/Calibration
    Inc/Inverter/Drivers/GateDriver
    Inc/Inverter/Drivers/CAN
    Inc/Inverter/Drivers/UART
    Inc/Inverter/Drivers/Storage
    Inc/Inverter/Drivers/Logging
    Inc/Inverter/Drivers/PWM
    Inc/Inverter/Drivers/Sensors
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    HSE_VALUE=8000000U
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
