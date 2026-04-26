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
set(CMAKE_PROJECT_NAME Summer_26_zgt6_freertos)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME}
        Code/01_Task/Inc/task_chassis.h
        Code/01_Task/Src/task_chassis.c
)

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Keep active sources explicit. Anything moved into 99_Archive is retained in
# the repository for future reuse, but excluded from the default build.
set(ACTIVE_TASK_SOURCES
        Code/01_Task/Src/task_init.c
        Code/01_Task/Src/task_gimbal.c
        Code/01_Task/Src/task_debug.c
)

set(ACTIVE_MODULE_SOURCES
        Code/02_Module/Src/mod_vofa.c
        Code/02_Module/Src/mod_vision.c
        Code/02_Module/Src/mod_k230.c
        Code/02_Module/Src/mod_uart_guard.c
        Code/02_Module/Src/mod_battery.c
        Code/02_Module/Src/mod_led.c
        Code/02_Module/Src/mod_key.c
        Code/02_Module/Src/mod_relay.c
        Code/02_Module/Src/mod_motor.c
        Code/02_Module/Src/mod_sensor.c
        Code/02_Module/Src/mod_stepper.c
        Code/02_Module/Src/mod_oled.c
        Code/02_Module/Src/mod_oled_data.c
)

set(ACTIVE_COMMON_SOURCES
        Code/03_Common/Src/common_str.c
        Code/03_Common/Src/common_checksum.c
        Code/03_Common/PID/pid_inc.c
        Code/03_Common/PID/pid_pos.c
        Code/03_Common/PID/pid_multi.c
)

set(ACTIVE_DRIVER_SOURCES
        Code/04_Driver/Src/drv_gpio.c
        Code/04_Driver/Src/drv_key.c
        Code/04_Driver/Src/drv_uart.c
        Code/04_Driver/Src/drv_adc.c
        Code/04_Driver/Src/drv_pwm.c
        Code/04_Driver/Src/drv_encoder.c
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        ${ACTIVE_TASK_SOURCES}
        ${ACTIVE_MODULE_SOURCES}
        ${ACTIVE_COMMON_SOURCES}
        ${ACTIVE_DRIVER_SOURCES}
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        Core/Inc
        Code/01_Task/Inc
        Code/02_Module/Inc
        Code/03_Common/Inc
        Code/03_Common/PID
        Code/04_Driver/Inc

)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

if(CMAKE_OBJCOPY)
    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O ihex
                $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/${CMAKE_PROJECT_NAME}.hex
        COMMAND ${CMAKE_OBJCOPY} -O binary
                $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/${CMAKE_PROJECT_NAME}.bin
        COMMENT "Generating ${CMAKE_PROJECT_NAME}.hex and ${CMAKE_PROJECT_NAME}.bin"
    )
endif()
