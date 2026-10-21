# Installed by third_party/CMakeLists.txt's LuaJIT step at lib/cmake/luajit/luajitConfig.cmake. Found by
# find_package(luajit CONFIG REQUIRED) (cmake/BitoscopeDeps.cmake); provides bitoscope::luajit.
#
# Unlike every other dependency in this deps build, LuaJIT has no CMake build of its own (it is
# built by src/msvcbuild.bat or its makefile - see build_luajit.cmake), so there is no
# install(EXPORT) to generate this file: it is hand-written, and declares the imported target
# itself from the files install_luajit.cmake copied into this same prefix
# (../../lib/luajit[d].lib or ../../lib/libluajit[d].a, ../../../include/luajit-2.1).

if(NOT TARGET bitoscope::luajit)
  add_library(bitoscope::luajit STATIC IMPORTED)

  if(WIN32)
    set(_luajit_release "${CMAKE_CURRENT_LIST_DIR}/../../luajit.lib")
    set(_luajit_debug   "${CMAKE_CURRENT_LIST_DIR}/../../luajitd.lib")
  else()
    set(_luajit_release "${CMAKE_CURRENT_LIST_DIR}/../../libluajit.a")
    set(_luajit_debug   "${CMAKE_CURRENT_LIST_DIR}/../../libluajitd.a")
  endif()

  if(EXISTS "${_luajit_release}")
    set_property(TARGET bitoscope::luajit APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
    set_target_properties(bitoscope::luajit PROPERTIES IMPORTED_LOCATION_RELEASE "${_luajit_release}")
  endif()
  if(EXISTS "${_luajit_debug}")
    set_property(TARGET bitoscope::luajit APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
    set_target_properties(bitoscope::luajit PROPERTIES IMPORTED_LOCATION_DEBUG "${_luajit_debug}")
  endif()

  set_target_properties(bitoscope::luajit PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../../include/luajit-2.1"
  )
  # On Linux, loadlib-style dlopen() support (LuaJIT's FFI and package loader) needs libdl; on
  # some distributions math functions need explicit -lm. UNVERIFIED (Windows only, this step).
  if(UNIX AND NOT APPLE)
    set_property(TARGET bitoscope::luajit APPEND PROPERTY
      INTERFACE_LINK_LIBRARIES m ${CMAKE_DL_LIBS})
  endif()

  unset(_luajit_release)
  unset(_luajit_debug)
endif()
