# SPDX-License-Identifier: MIT

set(BITOSCOPE_LUA_EMBED_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/embed_lua.cmake" CACHE INTERNAL "")

function(bitoscope_embed_lua out_cpp function_name)
  list(JOIN ARGN "|" _files)
  add_custom_command(
    OUTPUT "${out_cpp}"
    COMMAND ${CMAKE_COMMAND} "-DOUT=${out_cpp}" "-DFUNCTION=${function_name}" "-DFILES=${_files}"
            -P "${BITOSCOPE_LUA_EMBED_SCRIPT}"
    DEPENDS ${ARGN} "${BITOSCOPE_LUA_EMBED_SCRIPT}"
    COMMENT "Embedding Lua scripts into ${out_cpp}"
    VERBATIM
  )
endfunction()

bitoscope_embed_lua("${CMAKE_CURRENT_BINARY_DIR}/host_lua_luajit.cpp" hostScripts
    "${CMAKE_CURRENT_SOURCE_DIR}/gateway_host.lua"
    "${CMAKE_CURRENT_SOURCE_DIR}/sig_ffi.lua")

file(GLOB BITOSCOPE_LUA_BUILTIN_SCRIPTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/*.lua")
list(SORT BITOSCOPE_LUA_BUILTIN_SCRIPTS)
bitoscope_embed_lua("${CMAKE_CURRENT_BINARY_DIR}/builtin_scripts_luajit.cpp" builtinScripts
    ${BITOSCOPE_LUA_BUILTIN_SCRIPTS})

add_library(bitoscope_lua_gateway_luajit STATIC
    gateway.cpp
    script_state.cpp
    sig_ffi.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/host_lua_luajit.cpp"
)
target_include_directories(bitoscope_lua_gateway_luajit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bitoscope_lua_gateway_luajit PUBLIC bitoscope_decoder_c_sdk bitoscope::luajit
                                                    PRIVATE bitoscope_plugin_ipc bitoscope_decoder_c_sdk_internal)
target_compile_features(bitoscope_lua_gateway_luajit PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_lua_gateway_luajit)

add_executable(decoders_luajit
    main.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/builtin_scripts_luajit.cpp"
)
target_link_libraries(decoders_luajit PRIVATE bitoscope_lua_gateway_luajit)
target_compile_features(decoders_luajit PRIVATE cxx_std_23)
bitoscope_enable_warnings(decoders_luajit)
bitoscope_decoder_plugin_executable(decoders_luajit)
set_property(TARGET decoders_luajit PROPERTY BITOSCOPE_BUILTIN_SCRIPTS ${BITOSCOPE_LUA_BUILTIN_SCRIPTS})
