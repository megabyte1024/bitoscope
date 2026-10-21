# SPDX-License-Identifier: MIT

add_library(bitoscope_gui_task_pool STATIC
    task_pool.cpp
    task_pool.h
)

target_include_directories(bitoscope_gui_task_pool PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Threads REQUIRED)
target_link_libraries(bitoscope_gui_task_pool PUBLIC Threads::Threads)

target_compile_features(bitoscope_gui_task_pool PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_gui_task_pool)
