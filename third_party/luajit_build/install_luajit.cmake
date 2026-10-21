# Run (cmake -P) as the INSTALL step of the "luajit" ExternalProject in third_party/CMakeLists.txt,
# once per configuration, after build_luajit.cmake staged that configuration's library and headers.
# Copies the staged files into the deps prefix and drops the hand-written luajitConfig.cmake next
# to them (LuaJIT has no CMake package of its own to install(EXPORT) - see that file's comment).
#
# Inputs (set with -D on the command line):
#   LUAJIT_STAGE_DIR                the directory build_luajit.cmake staged into
#   BITOSCOPE_LUAJIT_INSTALL_PREFIX the deps prefix (CMAKE_INSTALL_PREFIX of the deps build)
#   LUAJIT_CONFIG_TEMPLATE          third_party/luajit_build/luajitConfig.cmake

if(NOT DEFINED LUAJIT_STAGE_DIR OR NOT EXISTS "${LUAJIT_STAGE_DIR}/lib")
  message(FATAL_ERROR
    "LUAJIT_STAGE_DIR ('${LUAJIT_STAGE_DIR}') has no lib/ - build_luajit.cmake did not run first.")
endif()
if(NOT DEFINED BITOSCOPE_LUAJIT_INSTALL_PREFIX)
  message(FATAL_ERROR "BITOSCOPE_LUAJIT_INSTALL_PREFIX was not set.")
endif()
if(NOT DEFINED LUAJIT_CONFIG_TEMPLATE OR NOT EXISTS "${LUAJIT_CONFIG_TEMPLATE}")
  message(FATAL_ERROR "LUAJIT_CONFIG_TEMPLATE ('${LUAJIT_CONFIG_TEMPLATE}') does not exist.")
endif()

file(INSTALL "${LUAJIT_STAGE_DIR}/lib/" DESTINATION "${BITOSCOPE_LUAJIT_INSTALL_PREFIX}/lib")
file(INSTALL "${LUAJIT_STAGE_DIR}/include/" DESTINATION "${BITOSCOPE_LUAJIT_INSTALL_PREFIX}/include")
file(INSTALL "${LUAJIT_CONFIG_TEMPLATE}"
  DESTINATION "${BITOSCOPE_LUAJIT_INSTALL_PREFIX}/lib/cmake/luajit")
