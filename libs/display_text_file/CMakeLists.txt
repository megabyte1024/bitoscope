# SPDX-License-Identifier: MIT

# WHY: nlohmann/json stays PRIVATE: a large header, and this library is linked into every plugin executable.
add_library(bitoscope_display_text_file STATIC
    display_text_file.cpp
    display_text_file.h
)

target_include_directories(bitoscope_display_text_file PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(bitoscope_display_text_file PUBLIC cxx_std_23)
target_link_libraries(bitoscope_display_text_file PUBLIC bitoscope_display_text)

find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(bitoscope_display_text_file PRIVATE nlohmann_json::nlohmann_json)
if(WIN32)
  target_link_libraries(bitoscope_display_text_file PRIVATE shell32 ole32)
endif()
bitoscope_enable_warnings(bitoscope_display_text_file)
