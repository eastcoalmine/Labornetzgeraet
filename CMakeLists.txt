# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(LCD_1602 C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(LCD_1602 LCD_1602.c lcd1602_RGB_Module.c )

pico_set_program_name(LCD_1602 "LCD_1602")
pico_set_program_version(LCD_1602 "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(LCD_1602 0)
pico_enable_stdio_usb(LCD_1602 1)

# Add the standard library to the build
target_link_libraries(LCD_1602
        pico_stdlib
        hardware_i2c
        hardware_adc)

# Add the standard include files to the build
target_include_directories(LCD_1602 PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${USERHOME}/.pico-sdk/sdk/2.2.0/src/rp2_common/hardware_adc/include
)

pico_add_extra_outputs(LCD_1602)