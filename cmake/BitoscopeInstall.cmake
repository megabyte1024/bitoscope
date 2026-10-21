# SPDX-License-Identifier: MIT

# WHY: Package builds configure with -DBITOSCOPE_GUI_TEST_HOOK=OFF: shipped program must not carry test hook.

install(TARGETS bitoscope_cli RUNTIME DESTINATION .)

function(bitoscope_install_license source installed_name)
  install(FILES "${CMAKE_SOURCE_DIR}/${source}" DESTINATION licenses RENAME "${installed_name}"
          COMPONENT licenses)
endfunction()

bitoscope_install_license("LICENSE" "bitoscope-LICENSE.txt")
bitoscope_install_license("third_party/capnproto/LICENSE" "capnproto-LICENSE.txt")
bitoscope_install_license("third_party/hdf5/LICENSE" "hdf5-LICENSE.txt")
bitoscope_install_license("third_party/zlib/LICENSE" "zlib-LICENSE.txt")
bitoscope_install_license("third_party/lz4/lib/LICENSE" "lz4-LICENSE.txt")
bitoscope_install_license("third_party/luajit/COPYRIGHT" "luajit-LICENSE.txt")
bitoscope_install_license("third_party/nlohmann_json/LICENSE.MIT" "nlohmann_json-LICENSE.txt")

get_property(_bitoscope_install_plugins GLOBAL PROPERTY BITOSCOPE_PLUGIN_TARGETS)
if(_bitoscope_install_plugins)
  # WHY: Install whole plugin output directory: it is exactly the set GUI copies, plus vcpkg's libusb DLL.
  install(DIRECTORY "${BITOSCOPE_PLUGIN_OUTPUT_DIR}/" DESTINATION drivers USE_SOURCE_PERMISSIONS)
endif()

get_property(_bitoscope_install_decoders GLOBAL PROPERTY BITOSCOPE_DECODER_PLUGIN_TARGETS)
if(_bitoscope_install_decoders)
  install(DIRECTORY "${BITOSCOPE_DECODER_OUTPUT_DIR}/" DESTINATION decoders USE_SOURCE_PERMISSIONS)
endif()

if(BITOSCOPE_GUI_BUILT)
  install(TARGETS bitoscope_gui RUNTIME DESTINATION .)
  install(DIRECTORY "${CMAKE_SOURCE_DIR}/gui/assets/" DESTINATION assets
          PATTERN "*.desktop" EXCLUDE)

  # WHY: Exec has no path: flat layout does not put bitoscope_gui on PATH, so package or user does.
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(FILES "${CMAKE_SOURCE_DIR}/gui/assets/bitoscope.desktop" DESTINATION share/applications)
    install(FILES "${CMAKE_SOURCE_DIR}/gui/assets/app_icon/app_icon.svg"
            DESTINATION share/icons/hicolor/scalable/apps RENAME bitoscope.svg)
  endif()

  bitoscope_install_license("third_party/imgui/LICENSE.txt" "imgui-LICENSE.txt")
  bitoscope_install_license("third_party/plutovg/LICENSE" "plutovg-LICENSE.txt")
  bitoscope_install_license("third_party/plutovg/source/FTL.TXT" "freetype-LICENSE.txt")
  bitoscope_install_license("third_party/plutosvg/LICENSE" "plutosvg-LICENSE.txt")
  bitoscope_install_license("gui/assets/fonts/google_sans_flex/OFL.txt" "google_sans_flex-OFL.txt")
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    bitoscope_install_license("third_party/glfw/LICENSE.md" "glfw-LICENSE.txt")
  endif()

  set(_bitoscope_freetype_ack "${CMAKE_BINARY_DIR}/generated/FREETYPE_ACKNOWLEDGEMENT.txt")
  file(WRITE "${_bitoscope_freetype_ack}"
"Portions of this software are copyright (c) 1996-2020 The FreeType Project (www.freetype.org).
All rights reserved.

Required by the FreeType License (freetype-LICENSE.txt) for plutovg's rasterizer and stroker
(source/plutovg-ft-*), which bitoscope's GUI uses to render its SVG icons.
")
  install(FILES "${_bitoscope_freetype_ack}" DESTINATION licenses COMPONENT licenses)
endif()

if(WIN32)
  set(CPACK_GENERATOR "ZIP")
  set(CPACK_PACKAGE_NAME "bitoscope")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_PACKAGE_FILE_NAME "bitoscope-${PROJECT_VERSION}-win64")
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "bitoscope")
  set(CPACK_VERBATIM_VARIABLES TRUE)
  include(CPack)
endif()

# WHY: No Linux or macOS CPack generator: AppImage and .app bundle were never built or verified.
