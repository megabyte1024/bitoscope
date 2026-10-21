# Run (cmake -P) as the BUILD step of the "luajit" ExternalProject in third_party/CMakeLists.txt,
# once per configuration. LuaJIT ships no CMake build of its own: on MSVC it is built by
# src/msvcbuild.bat, and elsewhere by its own makefile.
# Verified on Linux (Ubuntu 24.04, GCC 13.3). Still UNVERIFIED on macOS.
#
# Inputs (set with -D on the command line):
#   LUAJIT_SOURCE_DIR  third_party/luajit (the submodule); never written to
#   LUAJIT_BUILD_DIR   a per-configuration directory that receives a fresh copy of the source tree,
#                      where LuaJIT's own build runs; no two configurations may share one
#   LUAJIT_STAGE_DIR   a per-configuration directory this script stages its output into; the
#                      INSTALL step (install_luajit.cmake) copies it into the deps prefix
#   LUAJIT_CONFIG      Debug, Release, ... ($<CONFIG>)

if(NOT DEFINED LUAJIT_SOURCE_DIR OR NOT EXISTS "${LUAJIT_SOURCE_DIR}/src/lua.h")
  message(FATAL_ERROR
    "LUAJIT_SOURCE_DIR ('${LUAJIT_SOURCE_DIR}') does not look like the LuaJIT submodule "
    "(third_party/luajit). Run 'git submodule update --init third_party/luajit' first.")
endif()
if(NOT DEFINED LUAJIT_STAGE_DIR)
  message(FATAL_ERROR "LUAJIT_STAGE_DIR was not set.")
endif()
if(NOT DEFINED LUAJIT_BUILD_DIR)
  message(FATAL_ERROR "LUAJIT_BUILD_DIR was not set.")
endif()
if(NOT DEFINED LUAJIT_CONFIG)
  set(LUAJIT_CONFIG "Release")
endif()

# WHY: LuaJIT's make has no flag dependencies, so a shared tree hands one configuration's objects to the next.
file(REMOVE_RECURSE "${LUAJIT_BUILD_DIR}")
file(MAKE_DIRECTORY "${LUAJIT_BUILD_DIR}")
file(COPY "${LUAJIT_SOURCE_DIR}/" DESTINATION "${LUAJIT_BUILD_DIR}" PATTERN ".git" EXCLUDE)

# WHY: without .git the build reads its version from .relver, so the copy gets the submodule's commit time.
find_program(_git git)
if(_git)
  execute_process(
    COMMAND "${_git}" -C "${LUAJIT_SOURCE_DIR}" show -s --format=%ct
    OUTPUT_VARIABLE _relver
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _git_rc
    ERROR_QUIET
  )
  if(_git_rc EQUAL 0 AND _relver MATCHES "^[0-9]+$")
    file(WRITE "${LUAJIT_BUILD_DIR}/.relver" "${_relver}\n")
  endif()
endif()

set(_src "${LUAJIT_BUILD_DIR}/src")
if(LUAJIT_CONFIG STREQUAL "Debug")
  set(_debug TRUE)
else()
  set(_debug FALSE)
endif()

if(WIN32)
  # msvcbuild.bat's own options must be given in order: [nogc64] [lua52compat] [debug] [static].
  # It needs cl.exe/link.exe/lib.exe on PATH (an MSVC developer environment); when driven from a
  # Visual Studio generator's own build, MSBuild's toolset environment already provides that to
  # every custom step's child processes, which is what this relies on.
  if(_debug)
    set(_bat_args debug static)
  else()
    set(_bat_args static)
  endif()
  # This machine's cmd.exe does not search the current directory for a bare command name at all
  # (confirmed by hand: "minilua.exe" alone fails with "is not recognized" even run directly in
  # its own directory, with or without NoDefaultCurrentDirectoryInPath set; ".\minilua.exe"
  # finds it, and so does a bare name once the directory is put on PATH). msvcbuild.bat both IS
  # invoked by a bare name here and, worse, invokes its OWN freshly-built tools (minilua, buildvm)
  # by bare name internally, which cannot be fixed by quoting this call alone. Putting ${_src} on
  # PATH for this one process (and everything it spawns, since PATH is inherited down the tree)
  # fixes every one of those lookups at once, without patching the vendored script.
  #
  # THE C RUNTIME.
  # msvcbuild.bat's static build passes no /M option at all, so cl.exe takes its default, the static
  # release runtime (/MT): every object said /DEFAULTLIB:LIBCMT, and a project executable (/MDd in
  # Debug, /MD in Release) mixed two runtimes (LNK4098). cl.exe appends the environment variable _CL_
  # to every command line it runs, so it gives the batch file's compiler the project's runtime
  # without patching the vendored script: /MDd for Debug, /MD otherwise. /Z7 with it: the debug
  # information goes INTO the objects (the deps build's convention, CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
  # Embedded) instead of msvcbuild.bat's /Zi, whose .pdb stays in the build copy's src/ and is not
  # installed - so a Debug link that reads it would warn LNK4099 once that checkout is gone. cl.exe
  # says "D9025: overriding '/Zi' with '/Z7'" for each file; that is this, and expected. The tools
  # the batch file builds and runs for itself (minilua, buildvm) get the same runtime, which every
  # machine with MSVC has.
  if(_debug)
    set(_cl_extra "/MDd /Z7")
  else()
    set(_cl_extra "/MD /Z7")
  endif()
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E env "PATH=${_src};$ENV{PATH}" "_CL_=${_cl_extra}" --
      cmd /c "${_src}/msvcbuild.bat" ${_bat_args}
    WORKING_DIRECTORY "${_src}"
    RESULT_VARIABLE _rc
  )
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
      "LuaJIT's msvcbuild.bat failed (exit ${_rc}) for ${LUAJIT_CONFIG}. It must be run where "
      "cl.exe is on PATH (an MSVC developer command prompt).")
  endif()
  set(_built_lib "${_src}/lua51.lib")
else()
  # Verified on Linux (Ubuntu 24.04, GCC 13.3): LuaJIT's own makefile builds Debug and Release
  # cleanly with these variables (BUILDMODE to skip the shared library;
  # CCDEBUG/XCFLAGS/CCOPT for a debug build). Still UNVERIFIED on macOS.
  set(_make_args BUILDMODE=static)
  if(_debug)
    list(APPEND _make_args "CCDEBUG=-g" "CCOPT=-O0" "XCFLAGS=-DLUA_USE_ASSERT -DLUA_USE_APICHECK")
  endif()
  execute_process(
    COMMAND make ${_make_args}
    WORKING_DIRECTORY "${LUAJIT_BUILD_DIR}"
    RESULT_VARIABLE _rc
  )
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "LuaJIT 'make' failed (exit ${_rc}) for ${LUAJIT_CONFIG}. UNVERIFIED path.")
  endif()
  set(_built_lib "${_src}/libluajit.a")
endif()

if(NOT EXISTS "${_built_lib}")
  message(FATAL_ERROR "LuaJIT build did not produce ${_built_lib}")
endif()

file(MAKE_DIRECTORY "${LUAJIT_STAGE_DIR}/lib")
file(MAKE_DIRECTORY "${LUAJIT_STAGE_DIR}/include/luajit-2.1")

# The debug postfix "d" matches every other library in this deps build (third_party/CMakeLists.txt,
# CMAKE_DEBUG_POSTFIX), applied by hand here since LuaJIT's own build has no CMake target to carry
# that property automatically.
if(WIN32)
  if(_debug)
    set(_staged_name "luajitd.lib")
  else()
    set(_staged_name "luajit.lib")
  endif()
else()
  if(_debug)
    set(_staged_name "libluajitd.a")
  else()
    set(_staged_name "libluajit.a")
  endif()
endif()
file(COPY_FILE "${_built_lib}" "${LUAJIT_STAGE_DIR}/lib/${_staged_name}")

foreach(_h lua.h lauxlib.h lualib.h luaconf.h luajit.h)
  file(COPY_FILE "${_src}/${_h}" "${LUAJIT_STAGE_DIR}/include/luajit-2.1/${_h}")
endforeach()
