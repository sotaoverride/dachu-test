cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)

project(pico_w_http C CXX ASM)
pico_sdk_init()

add_executable(pico_w_http main.c)

# Tell the compiler to look in the current directory for lwipopts.h
target_include_directories(pico_w_http PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/../pico/pico-examples/pico_w/wifi)

# Use the background architecture for best stability
target_link_libraries(pico_w_http 
    pico_stdlib 
    pico_cyw43_arch_lwip_threadsafe_background
)

pico_enable_stdio_usb(pico_w_http 0)
pico_enable_stdio_uart(pico_w_http 1)
pico_add_extra_outputs(pico_w_http)
