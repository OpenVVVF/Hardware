cmake_minimum_required(VERSION 3.13)

# Import SDK before project()
if(NOT DEFINED ENV{PICO_SDK_PATH})
    message(FATAL_ERROR "PICO_SDK_PATH environment variable not set")
endif()

set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico_project C CXX ASM)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()

# Define source directory
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Source)

# Add your executable
add_executable(${PROJECT_NAME}
    ${SOURCE_DIR}/Main.cpp
    ${SOURCE_DIR}/RtBridge.cpp
    ${SOURCE_DIR}/Switching/CommutationManager.cpp

    ${SOURCE_DIR}/Switching/OpenLoopPwmDriver.cpp
    ${SOURCE_DIR}/Switching/ThreePhasePwmBridge.cpp

    # ${SOURCE_DIR}/Configuration/ConfigManager.cpp

    # ${SOURCE_DIR}/FileSystem/FileSystem.cpp

    ${SOURCE_DIR}/Sensors/MeasurementSystem.cpp
    ${SOURCE_DIR}/Sensors/MAX2253x.cpp
     ${SOURCE_DIR}/Telemetry.cpp


    # ${SOURCE_DIR}/svpwm.c
    ${SOURCE_DIR}/Command/CommandInitializer.cpp
    ${SOURCE_DIR}/Command/CommandManager.cpp
    ${SOURCE_DIR}/Command/Commands/AutoCommand.cpp
    ${SOURCE_DIR}/Command/Commands/FlashCommand.cpp
    ${SOURCE_DIR}/Command/Commands/CarrierCommand.cpp
    ${SOURCE_DIR}/Command/Commands/EmergencyStopCommand.cpp
    ${SOURCE_DIR}/Command/Commands/EnableCommand.cpp
    ${SOURCE_DIR}/Command/Commands/FreqCommand.cpp
    ${SOURCE_DIR}/Command/Commands/ImmediateCommand.cpp
    ${SOURCE_DIR}/Command/Commands/RampCommand.cpp
    ${SOURCE_DIR}/Command/Commands/AsyncCommand.cpp
    ${SOURCE_DIR}/Command/Commands/SoftStopCommand.cpp
    ${SOURCE_DIR}/Command/Commands/HelpCommand.cpp
    

    
    
)

# add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/pico-littlefs)

# pico_generate_pio_header(${PROJECT_NAME} ${SOURCE_DIR}/svpwm_phase.pio)
# pico_generate_pio_header(${PROJECT_NAME} ${SOURCE_DIR}/complementary_pwm.pio)
# pico_generate_pio_header(${PROJECT_NAME} ${SOURCE_DIR}/main.pio)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/Source
    ${CMAKE_CURRENT_LIST_DIR}/ThirdParty
)

# Link libraries
target_link_libraries(${PROJECT_NAME}
    pico_stdlib
    pico_multicore
    pico_rand
    pico_stdio_usb
    hardware_spi
    hardware_pio
    hardware_gpio
    hardware_timer
    hardware_sync
    hardware_pwm
)

# Enable USB output, disable UART
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

# Generate UF2 file
pico_add_extra_outputs(${PROJECT_NAME})

# Set USB vendor/product strings
target_compile_definitions(${PROJECT_NAME} PUBLIC
    USBD_DESC_STR_MAXLEN=64
    USBD_PID=0x000a
    USBD_VID=0x2e8a
    USBD_MANUFACTURER="RTE"
    USBD_PRODUCT="RTE-800V300A" # finalize naming schema later
)

# === CUSTOM TARGETS FOR TOOLS/SCRIPTS ===

# Build target
add_custom_target(build
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${PROJECT_NAME}
    COMMENT "Building project..."
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Clean target
add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target clean
    COMMAND rm -f ${PROJECT_NAME}.uf2 ${PROJECT_NAME}.bin ${PROJECT_NAME}.hex
    COMMENT "Cleaning build files..."
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Flash target
add_custom_target(flash
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tools/Flash.sh
    DEPENDS ${PROJECT_NAME}
    COMMENT "Flashing device..."
)

# Build and flash target
add_custom_target(build-flash
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tools/Build_Flash.sh
    COMMENT "Building and flashing..."
)

# Print info target (useful for debugging)
add_custom_target(info
    COMMAND echo "Project: ${PROJECT_NAME}"
    COMMAND echo "UF2 Path: ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.uf2"
    COMMAND echo "Build Dir: ${CMAKE_BINARY_DIR}"
    COMMAND ls -la ${CMAKE_BINARY_DIR}/*.uf2 2>/dev/null || echo "No UF2 file found"
    COMMENT "Project information..."
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)