# The "deps" build: the vendored third-party libraries, compiled ONCE from
# their git submodules and installed into deps/, so no project build tree compiles them again.
#
#   cmake -S third_party -B build-deps -DCMAKE_INSTALL_PREFIX=<repo>/deps
#   cmake --build build-deps --config Debug   --target install
#   cmake --build build-deps --config Release --target install
#
# The project build then finds these libraries
# with find_package(... CONFIG) through CMAKE_PREFIX_PATH - see the root CMakeLists.txt.
#
# Built here: HDF5 (with zlib, which HDF5 builds from third_party/zlib itself),
# Cap'n Proto (its libraries plus the schema compiler, capnp and capnpc-c++, into deps/bin), and for
# the GUI plutovg, plutosvg and nlohmann/json, and on Linux GLFW (the
# Linux GUI layer's window) - plutovg, plutosvg and GLFW only with
# BITOSCOPE_DEPS_GUI (default ON; see below). Also LuaJIT;
# it ships no CMake build of its own, so it is driven by CMake code of ours instead of an upstream
# CMakeLists.txt - see "LuaJIT" below.
# lz4 (one C file) stays in the project build, and so does
# Dear ImGui, whose imconfig belongs to the application.
#
# Each library is its own ExternalProject: a separate CMake configure of its submodule, exactly as
# if it were built standalone. That is not a style choice. HDF5 skips its
# install and export rules when HDF5_EXTERNALLY_CONFIGURED is set; as separate projects every
# library installs its own CMake package config, and no library's cache options (HDF5 sets many)
# leak into another's.
#
# DEBUG AND RELEASE IN ONE PREFIX, told apart by a debug postfix ("d"): libhdf5d.lib, zsd.lib,
# kjd.lib, capnpd.lib next to their release names. MSVC cannot mix the debug and
# release runtimes in one link, so both configurations must be installed. One prefix, rather than
# deps/Debug and deps/Release, because the project is built with a MULTI-config generator (Visual
# Studio): one project tree builds Debug and Release, and CMAKE_PREFIX_PATH cannot differ per
# configuration. With one prefix, each installed package holds one targets file per configuration
# (e.g. CapnProtoTargets-debug.cmake and CapnProtoTargets-release.cmake), and CMake picks the matching
# library per configuration by itself. The executables (h5dump, capnp, capnpc-c++) have no
# postfix: the configuration installed LAST wins, so install Debug first, then Release (as the
# scripts do) and the tools are the optimised ones. They behave the same either way.
#
# A single-config generator (Makefiles, Ninja) builds one configuration per build tree: configure
# this directory twice, with -DCMAKE_BUILD_TYPE=Debug and =Release, into two build directories
# and the same CMAKE_INSTALL_PREFIX. NOT VERIFIED on Linux or macOS.
#
# Debug information of the Debug libraries is EMBEDDED in the .lib (/Z7, CMAKE_MSVC_DEBUG_
# INFORMATION_FORMAT=Embedded), not written to a separate compile-time .pdb that the install
# would have to carry next to the library: without that the project's Debug links warn (LNK4099)
# that the library's .pdb is missing, and the libraries cannot be stepped into.
cmake_minimum_required(VERSION 3.24)
project(bitoscope_deps NONE)

include(ExternalProject)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  get_filename_component(_default_prefix "${CMAKE_CURRENT_SOURCE_DIR}/../deps" ABSOLUTE)
  set(CMAKE_INSTALL_PREFIX "${_default_prefix}" CACHE PATH "Where the libraries are installed" FORCE)
endif()

# The submodules built here, as the manifest records them (write_deps_manifest.cmake). GLFW only
# on Linux: the Windows GUI layer is Win32 and the macOS one will be Cocoa (G4), so nothing else
# needs it, and the Windows deps build stays exactly as it was.
set(_bitoscope_dep_submodules capnproto hdf5 zlib nlohmann_json luajit)
set(_bitoscope_dep_source_dirs "${CMAKE_CURRENT_SOURCE_DIR}")

# The libraries only the GUI needs: plutovg and plutosvg (its icons' rasterizer, libs/svg_raster) and,
# on Linux, GLFW. A deps build for the command line tool and the plugins alone leaves them out
# (-DBITOSCOPE_DEPS_GUI=OFF), and then needs none of GLFW's system packages
# (the Wayland and X11 development packages, pkg-config). The manifest records the choice, and a
# project configured with the GUI against such a deps/ stops with a message saying so
# (cmake/BitoscopeDeps.cmake).
option(BITOSCOPE_DEPS_GUI "Also build the libraries only the GUI needs (plutovg, plutosvg, and GLFW on Linux)" ON)
set(_bitoscope_dep_gui_dirs "")
if(BITOSCOPE_DEPS_GUI)
  list(APPEND _bitoscope_dep_submodules plutovg plutosvg)
  set(_bitoscope_dep_gui_dirs plutovg plutosvg)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND _bitoscope_dep_submodules glfw)
  endif()
endif()

foreach(_dep hdf5 zlib capnproto/c++ ${_bitoscope_dep_gui_dirs} nlohmann_json)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_dep}/CMakeLists.txt")
    message(FATAL_ERROR
      "third_party/${_dep} is empty - run 'git submodule update --init --recursive' first.")
  endif()
endforeach()
# luajit ships no CMakeLists.txt of its own (see below), so it is checked by a marker file instead.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/luajit/src/lua.h")
  message(FATAL_ERROR
    "third_party/luajit is empty - run 'git submodule update --init third_party/luajit' first.")
endif()

get_property(_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _multi_config AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Settings every library is configured with. Written to an initial-cache file (-C) rather than
# passed as -D arguments, so that the generator expression below reaches the library's own
# configure unevaluated (ExternalProject evaluates generator expressions in CMAKE_ARGS itself).
#
# CMAKE_CXX_STANDARD 17 is the pin the root CMakeLists.txt used to give these libraries when they
# were subdirectories of the project build (it stays there too).
# Cap'n Proto needs at least 17; HDF5's C++ library is off.
set(_common_cache "${CMAKE_BINARY_DIR}/common-init.cmake")
file(WRITE "${_common_cache}" [=[
# Generated by third_party/CMakeLists.txt - settings shared by every deps library.
set(CMAKE_CXX_STANDARD 17 CACHE STRING "")
set(CMAKE_CXX_STANDARD_REQUIRED ON CACHE BOOL "")
set(CMAKE_DEBUG_POSTFIX "d" CACHE STRING "")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "")
set(BUILD_TESTING OFF CACHE BOOL "")
set(CMAKE_POLICY_DEFAULT_CMP0141 NEW CACHE STRING "")
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>" CACHE STRING "")
]=])
file(APPEND "${_common_cache}" "set(CMAKE_INSTALL_PREFIX \"${CMAKE_INSTALL_PREFIX}\" CACHE PATH \"\")\n")
if(NOT _multi_config)
  file(APPEND "${_common_cache}" "set(CMAKE_BUILD_TYPE \"${CMAKE_BUILD_TYPE}\" CACHE STRING \"\")\n")
endif()

# How many compile jobs run at once: BITOSCOPE_BUILD_JOBS, never more than the physical CPU cores
# (default: all of them). One library builds at a time (USES_TERMINAL_* puts the steps in Ninja's
# one-job console pool; Unix Makefiles and MSBuild run them one by one unless told otherwise), and
# its own build gets that many jobs. Always a number: a bare `--parallel` lets the build tool pick,
# and Unix Makefiles' pick is `make -j` with no limit, which can run a machine out of memory.
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/BitoscopeBuildJobs.cmake")

# The Visual Studio generator compiles the files of one project one at a time unless /MP is given;
# HDF5 is one project of several hundred C files. The CL environment variable adds /MP<jobs> to
# every compiler call of the build without touching any library's flags, and one project builds at
# a time so the jobs do not multiply.
if(CMAKE_GENERATOR MATCHES "Visual Studio")
  set(_build_env ${CMAKE_COMMAND} -E env CL=/MP${BITOSCOPE_BUILD_JOBS})
  set(_build_parallel 1)
else()
  set(_build_env "")
  set(_build_parallel ${BITOSCOPE_BUILD_JOBS})
endif()

# One library: configured from its submodule with the common settings plus its own, then built and
# installed for the configuration being built ($<CONFIG>). BUILD_ALWAYS: the library's own build
# is asked every time and is a quick no-op when nothing changed, so a submodule moved to a new tag
# is rebuilt on the next deps build instead of being skipped on a stale stamp.
function(bitoscope_add_dep name source_dir own_cache_text)
  set(_own_cache "${CMAKE_BINARY_DIR}/${name}-init.cmake")
  file(WRITE "${_own_cache}" "include(\"${_common_cache}\")\n${own_cache_text}")
  ExternalProject_Add(${name}
    SOURCE_DIR "${source_dir}"
    PREFIX "${CMAKE_BINARY_DIR}/ep"
    BINARY_DIR "${CMAKE_BINARY_DIR}/${name}"
    CMAKE_ARGS -C "${_own_cache}"
    BUILD_COMMAND ${_build_env} ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG> --parallel ${_build_parallel}
    USES_TERMINAL_CONFIGURE ON
    USES_TERMINAL_BUILD ON
    INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR> --config $<CONFIG>
    BUILD_ALWAYS ON
    DEPENDS ${ARGN}
  )
endfunction()

# HDF5 2.2.0 (third_party/hdf5), static, with the options libs/hdf5_writer/CMakeLists.txt used when
# HDF5 was a subdirectory of the project build. zlib 1.3.2 (third_party/zlib) is built by HDF5
# itself (ZLIB_USE_EXTERNAL with the GIT mechanism) - pointed at the submodule by
# FETCHCONTENT_SOURCE_DIR_HDF5_ZLIB and fully disconnected, so nothing is downloaded - and
# installed next to it, with its own ZLIB package config. The tools (h5dump, the independent
# reader of the interoperability check in hdf5_writer_tests) are built and installed into deps/bin.
# Dropped from the old option set: HDF5_EXTERNALLY_CONFIGURED, which is what switched HDF5's
# install and export rules off. Added: HDF5_NO_PACKAGES (no CPack setup).
bitoscope_add_dep(hdf5 "${CMAKE_CURRENT_SOURCE_DIR}/hdf5" "
set(BUILD_STATIC_LIBS ON CACHE BOOL \"\")
set(HDF5_BUILD_EXAMPLES OFF CACHE BOOL \"\")
set(HDF5_BUILD_HL_LIB OFF CACHE BOOL \"\")
set(HDF5_BUILD_CPP_LIB OFF CACHE BOOL \"\")
set(HDF5_BUILD_FORTRAN OFF CACHE BOOL \"\")
set(HDF5_BUILD_JAVA OFF CACHE BOOL \"\")
set(HDF5_BUILD_DOC OFF CACHE BOOL \"\")
set(HDF5_BUILD_TOOLS ON CACHE BOOL \"\")
set(HDF5_BUILD_STATIC_TOOLS ON CACHE BOOL \"\")
set(HDF5_ENABLE_ZLIB_SUPPORT ON CACHE BOOL \"\")
set(HDF5_USE_ZLIB_STATIC ON CACHE BOOL \"\")
set(ZLIB_USE_EXTERNAL ON CACHE BOOL \"\")
set(HDF5_ALLOW_EXTERNAL_SUPPORT GIT CACHE STRING \"\")
set(FETCHCONTENT_SOURCE_DIR_HDF5_ZLIB \"${CMAKE_CURRENT_SOURCE_DIR}/zlib\" CACHE PATH \"\")
set(FETCHCONTENT_FULLY_DISCONNECTED ON CACHE BOOL \"\")
set(HDF5_NO_PACKAGES ON CACHE BOOL \"\")
")

# WHY: Private libraries (test framework) are built only where private dir exists; public tree has none.
include("${CMAKE_CURRENT_SOURCE_DIR}/../private/third_party/deps.cmake" OPTIONAL)

# Cap'n Proto 1.5.0 (third_party/capnproto; the CMake project is its c++ directory): kj, capnp and
# the rest of its static libraries, and the schema compiler (capnp, capnpc-c++) into deps/bin.
# OpenSSL and zlib support (kj-tls, kj-gzip) are OFF rather than AUTO, so the build never picks up
# whatever the machine happens to have (a hidden system dependency: a library that found vcpkg's
# zlib this way broke once that zlib was removed); the project uses serialization only.
bitoscope_add_dep(capnproto "${CMAKE_CURRENT_SOURCE_DIR}/capnproto/c++" "
set(WITH_OPENSSL OFF CACHE STRING \"\")
set(WITH_ZLIB OFF CACHE STRING \"\")
")

if(BITOSCOPE_DEPS_GUI)
# plutovg 1.3.3 (third_party/plutovg): the 2D vector rasterizer plutosvg draws with. C11, a dozen
# files; its examples are not built.
bitoscope_add_dep(plutovg "${CMAKE_CURRENT_SOURCE_DIR}/plutovg" "
set(PLUTOVG_BUILD_EXAMPLES OFF CACHE BOOL \"\")
")

# plutosvg 0.0.8 (third_party/plutosvg): the SVG renderer of the GUI's icons.
# It looks for an installed plutovg first and, failing that, falls back to add_subdirectory of its
# own nested plutovg submodule. That nested copy is NEVER used here: plutosvg is configured after
# the plutovg above is installed (DEPENDS), finds it in this install prefix, and
# CMAKE_REQUIRE_FIND_PACKAGE_plutovg turns its QUIET lookup into a required one, so a plutovg that
# cannot be found stops the configure instead of silently building a second, unpinned copy. Its
# installed plutosvgConfig.cmake finds plutovg again (find_dependency) from the same prefix. No
# FreeType integration, no examples.
bitoscope_add_dep(plutosvg "${CMAKE_CURRENT_SOURCE_DIR}/plutosvg" "
set(CMAKE_PREFIX_PATH \"${CMAKE_INSTALL_PREFIX}\" CACHE PATH \"\")
set(CMAKE_REQUIRE_FIND_PACKAGE_plutovg ON CACHE BOOL \"\")
set(PLUTOSVG_ENABLE_FREETYPE OFF CACHE BOOL \"\")
set(PLUTOSVG_BUILD_EXAMPLES OFF CACHE BOOL \"\")
" plutovg)
endif()

# nlohmann/json 3.12.0 (third_party/nlohmann_json): header-only, so nothing is compiled; its headers
# and its CMake package (nlohmann_json::nlohmann_json) are installed. The layout and preferences
# library includes it in exactly one source file. Its tests are not built.
bitoscope_add_dep(nlohmann_json "${CMAKE_CURRENT_SOURCE_DIR}/nlohmann_json" "
set(JSON_BuildTests OFF CACHE BOOL \"\")
set(JSON_Install ON CACHE BOOL \"\")
")

# LuaJIT (third_party/luajit, branch v2.1 - it has no recent release tags, so pinned to a commit).
# LuaJIT ships no CMake build, and its own
# build (src/msvcbuild.bat on MSVC, its makefile elsewhere) cannot be reduced to a plain
# add_library() - it bootstraps its own tools (minilua, buildvm) and generates several headers
# first. So it is NOT built through bitoscope_add_dep(): CONFIGURE_COMMAND is empty and
# BUILD_COMMAND/INSTALL_COMMAND run third_party/luajit_build's own driver scripts (cmake -P)
# instead of a CMAKE_ARGS configure. UNVERIFIED on Linux/macOS beyond what build_luajit.cmake
# documents: this step built and ran it on Windows/MSVC only.
ExternalProject_Add(luajit
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/luajit"
  PREFIX "${CMAKE_BINARY_DIR}/ep"
  BINARY_DIR "${CMAKE_BINARY_DIR}/luajit"
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ${CMAKE_COMMAND}
    -DLUAJIT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/luajit
    -DLUAJIT_BUILD_DIR=<BINARY_DIR>/src-$<CONFIG>
    -DLUAJIT_STAGE_DIR=<BINARY_DIR>/stage-$<CONFIG>
    -DLUAJIT_CONFIG=$<CONFIG>
    -P "${CMAKE_CURRENT_SOURCE_DIR}/luajit_build/build_luajit.cmake"
  USES_TERMINAL_BUILD ON
  INSTALL_COMMAND ${CMAKE_COMMAND}
    -DLUAJIT_STAGE_DIR=<BINARY_DIR>/stage-$<CONFIG>
    -DBITOSCOPE_LUAJIT_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
    -DLUAJIT_CONFIG_TEMPLATE=${CMAKE_CURRENT_SOURCE_DIR}/luajit_build/luajitConfig.cmake
    -P "${CMAKE_CURRENT_SOURCE_DIR}/luajit_build/install_luajit.cmake"
  BUILD_ALWAYS ON
)

# GLFW 3.5.1 (third_party/glfw), Linux only, and only with BITOSCOPE_DEPS_GUI: the window, input and
# Vulkan surface of the Linux GUI layer (gui/platform/linux/). In the deps build, not compiled in the project like Dear ImGui:
# The rule is that third-party code is built here, and Dear ImGui is the exception
# only because the APPLICATION's imconfig configures it; GLFW takes no configuration of the
# application's, so nothing ties it to the project build. Its configure also probes the system
# (wayland-scanner, pkg-config for Wayland, xkbcommon and the X11 extensions), which is better done
# once here than in every project tree. A static library with both window systems, X11 and Wayland:
# GLFW chooses one at run time (Wayland in a Wayland session, X11 otherwise; XDG_SESSION_TYPE=x11
# selects X11 through XWayland). It loads libwayland, libxkbcommon, libdecor and libX11 at run time
# rather than linking them, so only their headers are needed here; GLFW 3.5 carries the Wayland
# protocol files it needs (deps/wayland/), so wayland-protocols is not needed. Examples, tests and
# documentation are not built.
if(BITOSCOPE_DEPS_GUI AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/glfw/CMakeLists.txt")
    message(FATAL_ERROR
      "third_party/glfw is empty - run 'git submodule update --init --recursive' first.")
  endif()
  bitoscope_add_dep(glfw "${CMAKE_CURRENT_SOURCE_DIR}/glfw" "
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL \"\")
set(GLFW_BUILD_TESTS OFF CACHE BOOL \"\")
set(GLFW_BUILD_DOCS OFF CACHE BOOL \"\")
set(GLFW_INSTALL ON CACHE BOOL \"\")
set(GLFW_BUILD_X11 ON CACHE BOOL \"\")
set(GLFW_BUILD_WAYLAND ON CACHE BOOL \"\")
")
endif()

# HDF5's installed package config looks for zlib's in the wrong place for upstream zlib; see the
# script for the details. Runs after the libraries above were installed.
install(CODE "
  set(BITOSCOPE_DEPS_PREFIX \"\${CMAKE_INSTALL_PREFIX}\")
  include(\"${CMAKE_CURRENT_SOURCE_DIR}/install_hdf5_zlib_forwarder.cmake\")
")

# deps/bitoscope_deps.cmake: the marker the project build looks for, and a record of which
# submodule commit each library was installed from. The root CMakeLists.txt compares those with
# the submodules' current commits and stops when they differ, so a submodule moved to a new tag
# without rebuilding deps/ is caught at configure time instead of silently linking the old build.
# Written at INSTALL time (after the libraries above were installed), not at configure time.
install(CODE "
  set(BITOSCOPE_DEPS_SOURCE_DIRS \"${_bitoscope_dep_source_dirs}\")
  set(BITOSCOPE_DEPS_BUILT_SUBMODULES \"${_bitoscope_dep_submodules}\")
  set(BITOSCOPE_DEPS_GUI_BUILT \"${BITOSCOPE_DEPS_GUI}\")
  set(BITOSCOPE_DEPS_MANIFEST \"\${CMAKE_INSTALL_PREFIX}/bitoscope_deps.cmake\")
  include(\"${CMAKE_CURRENT_SOURCE_DIR}/write_deps_manifest.cmake\")
")
