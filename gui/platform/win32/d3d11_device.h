/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace bitoscope::gui::win32 {

class D3d11Device {
 public:
  enum class PresentResult {
    Shown,
    Occluded,
    DeviceLost
  };

  static std::expected<D3d11Device, std::string> create(HWND hwnd);

  ID3D11Device* device() const { return device_.Get(); }
  ID3D11DeviceContext* context() const { return context_.Get(); }

  std::expected<void, std::string> resize(UINT width, UINT height);

  void beginFrame(const std::array<float, 4>& clearRgba);

  PresentResult present();

  void setSyncInterval(UINT interval) { syncInterval_ = interval; }
  UINT syncInterval() const { return syncInterval_; }

  bool stillOccluded();

#ifdef BITOSCOPE_GUI_TEST_HOOK
  // WHY: Call AFTER the frame is drawn and BEFORE present(): a flip-model swap chain discards the buffer
  // when presented.
  struct CapturedImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;
  };
  std::expected<CapturedImage, std::string> captureBackBuffer();
#endif

 private:
  D3d11Device() = default;
  std::expected<void, std::string> createRenderTarget();

  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget_;
  UINT syncInterval_ = 1;
};

}
