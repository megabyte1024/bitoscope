/*
 * SPDX-License-Identifier: MIT
 */

#include "win32_platform.h"

#include <dwmapi.h>
#include <knownfolders.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <format>
#include <system_error>

#ifdef BITOSCOPE_GUI_TEST_HOOK
#include <save_name.h>
#endif

namespace bitoscope::gui::win32 {

namespace {

std::string toUtf8(const wchar_t* text, int length) {
  const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
  if (bytes <= 0) {
    return {};
  }
  std::string out(static_cast<std::size_t>(bytes), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, length, out.data(), bytes, nullptr, nullptr);
  return out;
}

}

std::filesystem::path roamingConfigDirectory() {
  PWSTR folder = nullptr;
  std::filesystem::path path;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &folder))) {
    path = std::filesystem::path(folder) / L"bitoscope";
  }
  CoTaskMemFree(folder);
  return path;
}

std::optional<std::filesystem::path> systemUiFontFile() {
  NONCLIENTMETRICSW metrics{};
  metrics.cbSize = sizeof(metrics);
  if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
    return std::nullopt;
  }
  const std::wstring faceName(metrics.lfMessageFont.lfFaceName);
  if (faceName.empty()) {
    return std::nullopt;
  }

  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                    L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", 0, KEY_READ,
                    &key) != ERROR_SUCCESS) {
    return std::nullopt;
  }

  std::wstring bestFile;
  const std::wstring exactTrueType = faceName + L" (TrueType)";
  const std::wstring exactOpenType = faceName + L" (OpenType)";
  for (DWORD index = 0;; ++index) {
    wchar_t name[512];
    DWORD nameLength = static_cast<DWORD>(std::size(name));
    wchar_t data[MAX_PATH];
    DWORD dataBytes = sizeof(data) - sizeof(wchar_t);
    DWORD type = 0;
    const LSTATUS status =
      RegEnumValueW(key, index, name, &nameLength, nullptr, &type, reinterpret_cast<BYTE*>(data),
                   &dataBytes);
    if (status == ERROR_NO_MORE_ITEMS) {
      break;
    }
    if (status != ERROR_SUCCESS || type != REG_SZ) {
      continue;
    }
    const std::size_t fileLength = dataBytes / sizeof(wchar_t);
    data[fileLength] = L'\0';
    const std::wstring valueName(name, nameLength);
    const std::wstring fileName(data);
    if (valueName == exactTrueType || valueName == exactOpenType) {
      bestFile = fileName;
      break;
    }
    if (bestFile.empty() && valueName.starts_with(faceName)) {
      bestFile = fileName;
    }
  }
  RegCloseKey(key);
  if (bestFile.empty()) {
    return std::nullopt;
  }

  std::filesystem::path path(bestFile);
  if (!path.is_absolute()) {
    PWSTR fontsFolder = nullptr;
    if (!SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Fonts, KF_FLAG_DEFAULT, nullptr, &fontsFolder))) {
      CoTaskMemFree(fontsFolder);
      return std::nullopt;
    }
    path = std::filesystem::path(fontsFolder) / bestFile;
    CoTaskMemFree(fontsFolder);
  }
  std::error_code ec;
  if (!std::filesystem::exists(path, ec) || ec) {
    return std::nullopt;
  }
  return path;
}

std::vector<gui_prefs::ScreenRect> monitorWorkAreas() {
  std::vector<gui_prefs::ScreenRect> areas;
  EnumDisplayMonitors(
    nullptr, nullptr,
    [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
      MONITORINFO info{};
      info.cbSize = sizeof(info);
      if (GetMonitorInfoW(monitor, &info)) {
        const RECT& r = info.rcWork;
        reinterpret_cast<std::vector<gui_prefs::ScreenRect>*>(data)->push_back(
          {r.left, r.top, r.right, r.bottom});
      }
      return TRUE;
    },
    reinterpret_cast<LPARAM>(&areas));
  return areas;
}

Win32PlatformServices::Win32PlatformServices(HWND hwnd, ID3D11Device* device,
                                             ID3D11DeviceContext* context, const RECT* normalRect,
                                             bool* fullScreen)
    : hwnd_(hwnd),
      device_(device),
      normalRect_(normalRect),
      fullScreen_(fullScreen),
      waveform_(device, context) {}

bool Win32PlatformServices::systemUsesDarkTheme() {
  DWORD value = 1;
  DWORD size = sizeof(value);
  const LSTATUS status = RegGetValueW(
    HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
    L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
  return status == ERROR_SUCCESS && value == 0;
}

std::vector<std::string> Win32PlatformServices::preferredUiLanguages() {
  ULONG count = 0;
  ULONG length = 0;
  if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &length) || length == 0) {
    return {};
  }
  std::wstring buffer(length, L'\0');
  if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, buffer.data(), &length)) {
    return {};
  }
  std::vector<std::string> tags;
  for (const wchar_t* p = buffer.c_str(); *p != L'\0'; p += wcslen(p) + 1) {
    tags.push_back(toUtf8(p, static_cast<int>(wcslen(p))));
  }
  return tags;
}

void Win32PlatformServices::setWindowFrameDark(bool dark) {
  const BOOL value = dark ? TRUE : FALSE;
  DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
}

std::vector<std::string> Win32PlatformServices::serialPorts() {
  std::vector<std::string> ports;
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &key) !=
      ERROR_SUCCESS) {
    return ports;
  }
  for (DWORD index = 0;; ++index) {
    wchar_t name[256];
    DWORD nameLength = static_cast<DWORD>(std::size(name));
    wchar_t data[256];
    DWORD dataBytes = sizeof(data) - sizeof(wchar_t);
    DWORD type = 0;
    const LSTATUS status = RegEnumValueW(key, index, name, &nameLength, nullptr, &type,
                                         reinterpret_cast<BYTE*>(data), &dataBytes);
    if (status == ERROR_NO_MORE_ITEMS) {
      break;
    }
    if (status != ERROR_SUCCESS || type != REG_SZ) {
      continue;
    }
    std::size_t length = dataBytes / sizeof(wchar_t);
    data[length] = L'\0';
    length = wcslen(data);
    if (length != 0) {
      ports.push_back(toUtf8(data, static_cast<int>(length)));
    }
  }
  RegCloseKey(key);
  return ports;
}

std::filesystem::path Win32PlatformServices::executableDirectory() {
  std::wstring path(MAX_PATH, L'\0');
  for (;;) {
    const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (n == 0) {
      return std::filesystem::current_path();
    }
    if (n < path.size()) {
      path.resize(n);
      return std::filesystem::path(path).parent_path();
    }
    path.resize(path.size() * 2);
  }
}

std::filesystem::path Win32PlatformServices::configDirectory() {
  return roamingConfigDirectory();
}

std::optional<gui_prefs::WindowPlacement> Win32PlatformServices::windowPlacement() {
  WINDOWPLACEMENT placement{};
  placement.length = sizeof(placement);
  if (!GetWindowPlacement(hwnd_, &placement)) {
    return std::nullopt;
  }
  const RECT& r = *normalRect_;
  if (r.right <= r.left || r.bottom <= r.top) {
    return std::nullopt;
  }
  const bool maximized =
    placement.showCmd == SW_SHOWMAXIMIZED ||
    (placement.showCmd == SW_SHOWMINIMIZED && (placement.flags & WPF_RESTORETOMAXIMIZED) != 0);
  return gui_prefs::WindowPlacement{.position = gui_prefs::WindowPosition{r.left, r.top},
                                    .width = r.right - r.left,
                                    .height = r.bottom - r.top,
                                    .maximized = maximized};
}

void Win32PlatformServices::requestClose() {
  PostMessageW(hwnd_, WM_CLOSE, 0, 0);
}

void Win32PlatformServices::setFullScreen(bool fullScreen) {
  if (fullScreen == *fullScreen_) {
    return;
  }
  if (fullScreen) {
    savedPlacement_.length = sizeof(savedPlacement_);
    if (!GetWindowPlacement(hwnd_, &savedPlacement_)) {
      return;
    }
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitor)) {
      return;
    }
    savedStyle_ = GetWindowLongPtrW(hwnd_, GWL_STYLE);
    // WHY: Set fullScreen_ BEFORE the resizing SetWindowPos so WM_WINDOWPOSCHANGED stops updating the normal rectangle.
    *fullScreen_ = true;
    SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    const RECT& r = monitor.rcMonitor;
    SetWindowPos(hwnd_, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top,
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
  } else {
    // WHY: Clear fullScreen_ BEFORE restoring so the WM_WINDOWPOSCHANGED the
    // restore triggers updates the normal rectangle again.
    *fullScreen_ = false;
    SetWindowLongPtrW(hwnd_, GWL_STYLE, savedStyle_);
    SetWindowPlacement(hwnd_, &savedPlacement_);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
  }
}

bool Win32PlatformServices::isFullScreen() {
  return *fullScreen_;
}

std::expected<ImTextureID, std::string> Win32PlatformServices::createTexture(
  std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba) {
  if (width == 0 || height == 0 ||
      rgba.size() != static_cast<std::size_t>(width) * height * 4) {
    return std::unexpected(std::format("bad texture size {} x {} for {} bytes", width, height,
                                       rgba.size()));
  }
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_IMMUTABLE;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA data{};
  data.pSysMem = rgba.data();
  data.SysMemPitch = width * 4;

  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
  HRESULT hr = device_->CreateTexture2D(&desc, &data, &texture);
  if (FAILED(hr)) {
    return std::unexpected(std::format("CreateTexture2D failed (HRESULT 0x{:08X})",
                                       static_cast<unsigned long>(hr)));
  }
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
  hr = device_->CreateShaderResourceView(texture.Get(), nullptr, &view);
  if (FAILED(hr)) {
    return std::unexpected(std::format("CreateShaderResourceView failed (HRESULT 0x{:08X})",
                                       static_cast<unsigned long>(hr)));
  }
  return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(view.Detach()));
}

void Win32PlatformServices::destroyTexture(ImTextureID texture) {
  if (texture != ImTextureID_Invalid) {
    reinterpret_cast<ID3D11ShaderResourceView*>(static_cast<std::uintptr_t>(texture))->Release();
  }
}

WaveformRenderer* Win32PlatformServices::waveformRenderer() {
  return &waveform_;
}

std::optional<std::filesystem::path> Win32PlatformServices::systemFontFile() {
  return systemUiFontFile();
}

std::optional<std::string> Win32PlatformServices::environmentVariable(std::string_view name) {
  const int wideLength = MultiByteToWideChar(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), nullptr, 0);
  std::wstring wideName(static_cast<std::size_t>(wideLength), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), wideName.data(), wideLength);
  const DWORD needed = GetEnvironmentVariableW(wideName.c_str(), nullptr, 0);
  if (needed == 0) {
    return std::nullopt;
  }
  std::wstring value(needed, L'\0');
  const DWORD length = GetEnvironmentVariableW(wideName.c_str(), value.data(), needed);
  if (length == 0 || length >= needed) {
    return std::nullopt;
  }
  return toUtf8(value.c_str(), static_cast<int>(length));
}

namespace {

std::wstring toWide(std::string_view text) {
  if (text.empty()) {
    return {};
  }
  const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>((std::max)(length, 0)), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length);
  return out;
}

class ComScope {
 public:
  ComScope() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)) {}
  ~ComScope() {
    if (SUCCEEDED(result_)) {
      CoUninitialize();
    }
  }
  ComScope(const ComScope&) = delete;
  ComScope& operator=(const ComScope&) = delete;
  bool usable() const { return SUCCEEDED(result_); }
  HRESULT result() const { return result_; }

 private:
  HRESULT result_;
};

std::string hresultText(const char* what, HRESULT hr) {
  return std::format("{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned long>(hr));
}

std::expected<std::optional<std::filesystem::path>, std::string> showFileDialog(
  HWND owner, REFCLSID kind, const PlatformServices::FileDialog& dialog, bool save) {
  ComScope com;
  if (!com.usable()) {
    return std::unexpected(hresultText("CoInitializeEx", com.result()));
  }
  Microsoft::WRL::ComPtr<IFileDialog> fileDialog;
  HRESULT hr = CoCreateInstance(kind, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fileDialog));
  if (FAILED(hr)) {
    return std::unexpected(hresultText("Creating file dialog", hr));
  }
  const std::wstring typeName = toWide(dialog.fileTypeName) + L" (*.h5)";
  const std::wstring allName = toWide(dialog.allFilesName.empty() ? std::string("*.*") : dialog.allFilesName);
  const COMDLG_FILTERSPEC types[] = {{typeName.c_str(), L"*.h5"}, {allName.c_str(), L"*.*"}};
  fileDialog->SetFileTypes(save ? 1u : 2u, types);
  fileDialog->SetFileTypeIndex(1);
  fileDialog->SetDefaultExtension(L"h5");
  const std::wstring title = toWide(dialog.title);
  fileDialog->SetTitle(title.c_str());
  FILEOPENDIALOGOPTIONS options = 0;
  if (SUCCEEDED(fileDialog->GetOptions(&options))) {
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
    options |= save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST;
    fileDialog->SetOptions(options);
  }
  std::error_code ec;
  if (!dialog.folder.empty() && std::filesystem::is_directory(dialog.folder, ec)) {
    Microsoft::WRL::ComPtr<IShellItem> folder;
    if (SUCCEEDED(SHCreateItemFromParsingName(dialog.folder.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
      fileDialog->SetFolder(folder.Get());
    }
  }
  if (save && !dialog.fileName.empty()) {
    const std::wstring name = toWide(dialog.fileName);
    fileDialog->SetFileName(name.c_str());
  }
  hr = fileDialog->Show(owner);
  if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
    return std::optional<std::filesystem::path>();
  }
  if (FAILED(hr)) {
    return std::unexpected(hresultText("File dialog", hr));
  }
  Microsoft::WRL::ComPtr<IShellItem> item;
  hr = fileDialog->GetResult(&item);
  if (FAILED(hr)) {
    return std::unexpected(hresultText("Reading file dialog's choice", hr));
  }
  PWSTR chosen = nullptr;
  hr = item->GetDisplayName(SIGDN_FILESYSPATH, &chosen);
  if (FAILED(hr) || chosen == nullptr) {
    CoTaskMemFree(chosen);
    return std::unexpected(hresultText("Reading chosen file's path", hr));
  }
  std::filesystem::path path(chosen);
  CoTaskMemFree(chosen);
  return std::optional<std::filesystem::path>(std::move(path));
}

}

std::filesystem::path Win32PlatformServices::preferencesFile() {
#ifdef BITOSCOPE_GUI_TEST_HOOK
  if (preferencesFileOverride_) {
    return *preferencesFileOverride_;
  }
#endif
  return PlatformServices::preferencesFile();
}

#ifdef BITOSCOPE_GUI_TEST_HOOK
void Win32PlatformServices::setPreferencesFile(std::filesystem::path file) {
  preferencesFileOverride_ = std::move(file);
}

void Win32PlatformServices::setTestDialogAnswers(bitoscope::gui_test_input::DialogAnswers* answers,
                                                 std::function<void(std::string_view)> log) {
  testDialogAnswers_ = answers;
  testDialogLog_ = std::move(log);
}

namespace {

std::expected<std::optional<std::filesystem::path>, std::string> answeredByTestHook(
  bitoscope::gui_test_input::DialogAnswers& answers, bitoscope::gui_test_input::DialogKind kind,
  const std::function<void(std::string_view)>& log) {
  const bitoscope::gui_test_input::DialogDecision decision = answers.take(kind);
  if (log) {
    log("test hook: " + decision.note);
  }
  if (!decision.path) {
    return std::optional<std::filesystem::path>();
  }
  const std::string path = kind == bitoscope::gui_test_input::DialogKind::Save
                             ? bitoscope::gui_save_name::withSessionExtension(*decision.path)
                             : *decision.path;
  return std::optional<std::filesystem::path>(std::filesystem::path(std::u8string(path.begin(), path.end())));
}

}
#endif

std::expected<std::optional<std::filesystem::path>, std::string> Win32PlatformServices::askSavePath(
  const FileDialog& dialog) {
#ifdef BITOSCOPE_GUI_TEST_HOOK
  if (testDialogAnswers_ != nullptr) {
    return answeredByTestHook(*testDialogAnswers_, bitoscope::gui_test_input::DialogKind::Save, testDialogLog_);
  }
#endif
  if (dialogVeil_) {
    dialogVeil_();
  }
  return showFileDialog(hwnd_, CLSID_FileSaveDialog, dialog, true);
}

std::expected<std::optional<std::filesystem::path>, std::string> Win32PlatformServices::askOpenPath(
  const FileDialog& dialog) {
#ifdef BITOSCOPE_GUI_TEST_HOOK
  if (testDialogAnswers_ != nullptr) {
    return answeredByTestHook(*testDialogAnswers_, bitoscope::gui_test_input::DialogKind::Open, testDialogLog_);
  }
#endif
  if (dialogVeil_) {
    dialogVeil_();
  }
  return showFileDialog(hwnd_, CLSID_FileOpenDialog, dialog, false);
}

void Win32PlatformServices::setDialogVeil(std::function<void()> veil) {
  dialogVeil_ = std::move(veil);
}

}
