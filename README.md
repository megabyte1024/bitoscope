# Bitoscope

Bitoscope is an open-source logic analyzer application.

## Features

### Drivers and decoders isolated from the application

- A driver or decoder that crashes, hangs or leaks memory does not affect the application.
- Drivers and decoders can be installed and updated independently of the application; older and
  newer versions can work together.

### Speed

- Designed for sample data rates up to 5 Gbit/s (USB 3).
- GPU-accelerated GUI, for captures of a billion samples and more.

### Small RAM footprint

- Captures are compressed in memory: typical bus traffic needs 4 to 7 times less RAM than raw
  samples, a quiet bus up to 30 times less.

### Several logic analyzers in parallel

- Several devices at the same time, up to 32 channels each.

### Cross-platform

- Linux and Windows; macOS is not supported yet.

### Protocol decoders

- Decoders are either native, with C ABI (built-in: I2C, SPI, UART), or Lua scripts run by
  LuaJIT.
- Decoder chains: a decoder can decode another decoder's output.

### More

- Open file format (HDF5).
- Support for additional functions of logic analyzers, such as built-in signal generators.

## Prerequisites

Common to every platform:

- CMake 3.24 or newer
- A C++23 compiler: GCC 13 or newer on Linux, MSVC from Visual Studio 2022 (or its Build Tools, with
  the "Desktop development with C++" workload) on Windows
- Git, to fetch the third-party libraries as submodules
- libusb 1.0, which the driver SDK links: a system package on Linux, vcpkg on Windows (below)

All other libraries (Cap'n Proto, HDF5, zlib, LZ4, LuaJIT, Dear ImGui, plutovg, plutosvg,
nlohmann/json and, on Linux, GLFW) come as git submodules and are built with the project.

### Linux

Package names are Debian/Ubuntu ones:

- `build-essential`, `cmake`, `git`
- `libusb-1.0-0-dev` and `pkg-config`
- For the GUI:
  - `libvulkan-dev` and a Vulkan driver (`mesa-vulkan-drivers` for AMD and Intel GPUs, or the
    vendor's driver)
  - `libdbus-1-dev`
  - for GLFW: `libwayland-dev`, `libwayland-bin`, `libxkbcommon-dev`, `libx11-dev`, `libxrandr-dev`,
    `libxinerama-dev`, `libxcursor-dev`, `libxi-dev`

```sh
sudo apt install build-essential cmake git pkg-config libusb-1.0-0-dev libvulkan-dev mesa-vulkan-drivers \
  libdbus-1-dev libwayland-dev libwayland-bin libxkbcommon-dev libx11-dev libxrandr-dev \
  libxinerama-dev libxcursor-dev libxi-dev
```

### Windows

- Visual Studio 2022 or its Build Tools (see above), CMake and Git for Windows.
- libusb from vcpkg, installed into `C:\vcpkg` (the same in Command Prompt and PowerShell):

  ```bat
  git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
  C:\vcpkg\bootstrap-vcpkg.bat
  C:\vcpkg\vcpkg install libusb:x64-windows
  ```

## How to compile

The third-party libraries are built once into `deps/` (again only when a submodule changes), then
the application, drivers and decoders against them.

### Linux

```sh
git clone --recursive https://github.com/megabyte1024/bitoscope.git
cd bitoscope
cmake -S third_party -B build-deps -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PWD/deps
cmake --build build-deps --target install
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel $(cmake -P cmake/BitoscopeBuildJobs.cmake)
```

A Debug build needs Debug libraries as well: a second deps build directory with
`-DCMAKE_BUILD_TYPE=Debug`, installed into the same `deps/`.

### Windows

Command Prompt:

```bat
git clone --recursive https://github.com/megabyte1024/bitoscope.git
cd bitoscope
cmake -S third_party -B build-deps -DCMAKE_INSTALL_PREFIX=%CD%/deps
cmake --build build-deps --config Release --target install
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_CONFIGURATION_TYPES=Release
cmake --build build --config Release
```

PowerShell:

```powershell
git clone --recursive https://github.com/megabyte1024/bitoscope.git
cd bitoscope
cmake -S third_party -B build-deps "-DCMAKE_INSTALL_PREFIX=$PWD/deps"
cmake --build build-deps --config Release --target install
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_CONFIGURATION_TYPES=Release
cmake --build build --config Release
```

### Run

```sh
./build/gui/bitoscope_gui
```

On Windows it is `build\gui\Release\bitoscope_gui.exe`.

## Architecture

```
                   bitoscope_gui
                   |
                   |  shared libraries (libs/): plugin host, acquisition,
                   |  capture memory, trigger, HDF5 files, decoder host
                   |
     stdin/stdout: Cap'n Proto control messages
     shared memory: samples
                   |
     +-------------+--------------+-------------------------+
     |                            |                         |
 driver plugin                driver plugin          decoder plugins
 (one process per device)     (another device)       (C++ and Lua)
```

- **Plugins are processes.** Drivers and decoders are plugins: executables the application starts
  itself, one process per device and one per decoder job. Drivers are found in `drivers/`, decoders
  in `decoders/`, next to the application.
- **Facets.** A plugin offers optional, independent interfaces called facets: logic analyzer
  acquisition, signal generator and decoder.
- **Versioning.** At start-up the application and the plugin negotiate the protocol version and the
  version of every facet.
- **Two channels per plugin.** Control messages (Cap'n Proto) go over the plugin's standard input
  and output; samples go through shared memory.
- **Decoding.** Decoder plugins receive a capture's samples and return decoded results; decoders can
  be chained.
- **Plugin SDK.** Drivers and decoders link a small SDK with a C interface (`bitoscope_driver.h`,
  `bitoscope_decoder.h`) and a thin C++ layer.

### Source layout

```
gui/           bitoscope_gui: app/ (shared GUI code), platform/win32 (Direct3D 11),
               platform/linux (GLFW, Vulkan), imgui/, assets/
libs/          the shared libraries:
  core/          plugin host
  plugin_ipc/    message framing and the Cap'n Proto layer every plugin uses
  driver_ipc/, decoder_ipc/          messages of the driver and decoder facets
  bulk_ring/, plugin_ring/, driver_ring/, decoder_ring/   the shared-memory rings
  plugin_c_sdk/, driver_c_sdk/, decoder_c_sdk/            the plugin SDKs (C interface)
  acquisition/   the host side of a capture
  capture/       capture memory
  trigger/       the software trigger
  decode/, decoder_host/             decoder chains and the host side of decoder plugins
  hdf5_writer/, hdf5_reader/         the capture file format
  gui_*/, waveform_*/                GUI building blocks
drivers/       driver plugins: demo/ (simulated device; signals/ holds its UART, SPI and I2C sources)
decoders/      decoder plugins: lowlevel/ (UART, SPI, I2C in C++),
               lua_gateway/ (the LuaJIT host and its built-in scripts)
proto/         the plugin protocol's Cap'n Proto schemas
cmake/         CMake helpers
third_party/   the third-party libraries, as git submodules, and their build
```

Drivers for real devices live in their own repositories and are built separately.

## Licence

MIT, see `LICENSE`. Third-party libraries keep their own licences.
