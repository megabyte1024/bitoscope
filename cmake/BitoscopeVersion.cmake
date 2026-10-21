# SPDX-License-Identifier: MIT

find_package(Git QUIET)

set(_bitoscope_git_available OFF)
set(_bitoscope_git_tagged OFF)
set(_bitoscope_git_dirty OFF)
set(_bitoscope_git_commit "")

if(GIT_EXECUTABLE)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_SOURCE_DIR}" rev-parse --short=7 HEAD
    OUTPUT_VARIABLE _bitoscope_git_commit
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _bitoscope_git_commit_result
    ERROR_QUIET
  )
  if(_bitoscope_git_commit_result EQUAL 0)
    set(_bitoscope_git_available ON)

    execute_process(
      COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_SOURCE_DIR}" describe --tags --always --dirty
      OUTPUT_VARIABLE _bitoscope_git_describe
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE _bitoscope_git_describe_result
      ERROR_QUIET
    )
    if(_bitoscope_git_describe_result EQUAL 0)
      string(FIND "${_bitoscope_git_describe}" "-dirty" _bitoscope_dirty_pos)
      if(NOT _bitoscope_dirty_pos EQUAL -1)
        set(_bitoscope_git_dirty ON)
      endif()
    endif()

    execute_process(
      COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_SOURCE_DIR}" describe --tags --exact-match
      OUTPUT_QUIET
      ERROR_QUIET
      RESULT_VARIABLE _bitoscope_git_exact_result
    )
    if(_bitoscope_git_exact_result EQUAL 0 AND NOT _bitoscope_git_dirty)
      set(_bitoscope_git_tagged ON)
    endif()
  else()
    set(_bitoscope_git_commit "")
  endif()
endif()

if(_bitoscope_git_available)
  if(_bitoscope_git_tagged)
    set(BITOSCOPE_GIT_KIND "tagged")
  else()
    set(BITOSCOPE_GIT_KIND "dev")
  endif()
else()
  set(BITOSCOPE_GIT_KIND "no_git")
endif()
set(BITOSCOPE_GIT_COMMIT "${_bitoscope_git_commit}")
if(_bitoscope_git_dirty)
  set(BITOSCOPE_GIT_DIRTY_INT 1)
else()
  set(BITOSCOPE_GIT_DIRTY_INT 0)
endif()

set(BITOSCOPE_VERSION_HEADER_DIR "${CMAKE_BINARY_DIR}/generated")
set(BITOSCOPE_VERSION_HEADER "${BITOSCOPE_VERSION_HEADER_DIR}/bitoscope_git_version.h")
configure_file(
  "${CMAKE_SOURCE_DIR}/cmake/bitoscope_git_version.h.in"
  "${BITOSCOPE_VERSION_HEADER}"
  @ONLY
)

message(STATUS "bitoscope version ${PROJECT_VERSION}, git ${BITOSCOPE_GIT_KIND} "
               "(commit '${BITOSCOPE_GIT_COMMIT}', dirty=${BITOSCOPE_GIT_DIRTY_INT})")
