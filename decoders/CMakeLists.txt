# SPDX-License-Identifier: MIT

foreach(_decoder_dir common lowlevel lua_gateway)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_decoder_dir}/CMakeLists.txt")
    add_subdirectory(${_decoder_dir})
  endif()
endforeach()
