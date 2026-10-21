# Run at install time by third_party/CMakeLists.txt (install(CODE)), after HDF5 was installed.
# Input: BITOSCOPE_DEPS_PREFIX (the install prefix).
#
# WHY: HDF5 2.2.0's installed hdf5-config.cmake loads zlib's package config from ITS OWN config
# directory (<prefix>/cmake/ZLIBConfig.cmake on Windows), and fails find_package(hdf5) with
# "Couldn't locate zlib targets file" when it is not there. That is where HDF5's default zlib - the
# HDF Group's fork, tag v1.3.2-cmakefix, which honours ZLIB_INSTALL_CMAKEDIR - would install it. The
# zlib this project builds is upstream zlib 1.3.2 (third_party/zlib), which always installs its config
# to <libdir>/cmake/zlib. HDF5 cannot be told (its ZLIB_VERSION is an INTERNAL cache entry, forced
# to "1.3.2-cmakefix"), so a one-line ZLIBConfig.cmake is written where HDF5 looks, forwarding to
# the real one. Nothing in either submodule is changed.
#
# A ZLIBConfig.cmake already there that is not this forwarder (a zlib that installs there itself)
# is left alone.

file(GLOB_RECURSE _hdf5_config "${BITOSCOPE_DEPS_PREFIX}/hdf5-config.cmake")
file(GLOB_RECURSE _zlib_config "${BITOSCOPE_DEPS_PREFIX}/ZLIBConfig.cmake")
list(FILTER _zlib_config INCLUDE REGEX "/cmake/zlib/ZLIBConfig\\.cmake$")
list(LENGTH _hdf5_config _hdf5_count)
list(LENGTH _zlib_config _zlib_count)
if(NOT _hdf5_count EQUAL 1 OR NOT _zlib_count EQUAL 1)
  message(FATAL_ERROR
    "Expected one hdf5-config.cmake and one <libdir>/cmake/zlib/ZLIBConfig.cmake under "
    "${BITOSCOPE_DEPS_PREFIX}, found: [${_hdf5_config}] and [${_zlib_config}]")
endif()

get_filename_component(_hdf5_dir "${_hdf5_config}" DIRECTORY)
set(_forwarder "${_hdf5_dir}/ZLIBConfig.cmake")
set(_marker "# bitoscope: forwards to upstream zlib's package config")
if(EXISTS "${_forwarder}")
  file(READ "${_forwarder}" _existing)
  string(FIND "${_existing}" "${_marker}" _at)
  if(_at EQUAL -1)
    message(STATUS "Kept (not ours): ${_forwarder}")
    return()
  endif()
endif()

file(RELATIVE_PATH _relative "${_hdf5_dir}" "${_zlib_config}")
message(STATUS "Writing: ${_forwarder}")
file(WRITE "${_forwarder}"
  "${_marker} (third_party/install_hdf5_zlib_forwarder.cmake).\n"
  "# HDF5's hdf5-config.cmake looks for zlib's config here; upstream zlib installs it elsewhere.\n"
  "include(\"\${CMAKE_CURRENT_LIST_DIR}/${_relative}\")\n")
