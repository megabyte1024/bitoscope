# SPDX-License-Identifier: MIT

set(_imgui "${CMAKE_SOURCE_DIR}/third_party/imgui")
if(NOT EXISTS "${_imgui}/imgui.cpp")
  message(FATAL_ERROR
    "third_party/imgui is empty - run 'git submodule update --init --recursive' first.")
endif()

add_library(bitoscope_imgui STATIC
    ${_imgui}/imgui.cpp
    ${_imgui}/imgui_draw.cpp
    ${_imgui}/imgui_tables.cpp
    ${_imgui}/imgui_widgets.cpp
    bitoscope_imconfig.h
)
if(WIN32)
  target_sources(bitoscope_imgui PRIVATE
      ${_imgui}/backends/imgui_impl_win32.cpp
      ${_imgui}/backends/imgui_impl_dx11.cpp
  )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_sources(bitoscope_imgui PRIVATE
      ${_imgui}/backends/imgui_impl_glfw.cpp
      ${_imgui}/backends/imgui_impl_vulkan.cpp
  )
  target_link_libraries(bitoscope_imgui PUBLIC glfw Vulkan::Vulkan)
  # WHY: The backend's own TU only: our code includes the backend header and needs the Vulkan prototypes.
  set_source_files_properties(${_imgui}/backends/imgui_impl_vulkan.cpp PROPERTIES
      COMPILE_DEFINITIONS IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
else()
  message(FATAL_ERROR "bitoscope_imgui: no Dear ImGui backends for ${CMAKE_SYSTEM_NAME} yet (G4)")
endif()

target_include_directories(bitoscope_imgui SYSTEM PUBLIC
    ${_imgui}
    ${_imgui}/backends
)
target_include_directories(bitoscope_imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(bitoscope_imgui PUBLIC IMGUI_USER_CONFIG="bitoscope_imconfig.h")

target_compile_features(bitoscope_imgui PUBLIC cxx_std_23)

# WHY: Windows backends link d3dcompiler, gdi32 and dwmapi through #pragma comment;
# d3d11 is linked by whoever makes the device.
