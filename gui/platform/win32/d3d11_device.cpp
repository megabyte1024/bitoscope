/*
 * SPDX-License-Identifier: MIT
 */

#include "d3d11_device.h"

#include <cstring>
#include <format>

namespace bitoscope::gui::win32 {

namespace {

std::string failure(const char* what, HRESULT hr) {
  return std::format("{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned long>(hr));
}

}

std::expected<D3d11Device, std::string> D3d11Device::create(HWND hwnd) {
  D3d11Device d;

  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
  const UINT flags = 0;
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                                 static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
                                 &d.device_, nullptr, &d.context_);
  if (hr == DXGI_ERROR_UNSUPPORTED) {
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels,
                           static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &d.device_,
                           nullptr, &d.context_);
  }
  if (FAILED(hr)) {
    return std::unexpected(failure("D3D11CreateDevice", hr));
  }

  Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
  Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
  Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
  if (hr = d.device_.As(&dxgiDevice); FAILED(hr)) {
    return std::unexpected(failure("ID3D11Device::QueryInterface(IDXGIDevice)", hr));
  }
  if (hr = dxgiDevice->GetAdapter(&adapter); FAILED(hr)) {
    return std::unexpected(failure("IDXGIDevice::GetAdapter", hr));
  }
  if (hr = adapter->GetParent(IID_PPV_ARGS(&factory)); FAILED(hr)) {
    return std::unexpected(failure("IDXGIAdapter::GetParent(IDXGIFactory2)", hr));
  }

  DXGI_SWAP_CHAIN_DESC1 desc{};
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 2;
  // WHY: Scaling NONE, not STRETCH: a late frame shows unscaled at top left, not squashed or stretched while resizing.
  desc.Scaling = DXGI_SCALING_NONE;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
  if (hr = factory->CreateSwapChainForHwnd(d.device_.Get(), hwnd, &desc, nullptr, nullptr,
                                           &d.swapChain_);
    FAILED(hr)) {
    return std::unexpected(failure("IDXGIFactory2::CreateSwapChainForHwnd", hr));
  }
  // WHY: No Alt+Enter exclusive full screen: the GUI is a desktop window.
  factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

  if (auto made = d.createRenderTarget(); !made) {
    return std::unexpected(made.error());
  }
  return d;
}

std::expected<void, std::string> D3d11Device::createRenderTarget() {
  Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
  HRESULT hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
  if (FAILED(hr)) {
    return std::unexpected(failure("IDXGISwapChain::GetBuffer", hr));
  }
  hr = device_->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTarget_);
  if (FAILED(hr)) {
    return std::unexpected(failure("ID3D11Device::CreateRenderTargetView", hr));
  }
  return {};
}

std::expected<void, std::string> D3d11Device::resize(UINT width, UINT height) {
  // WHY: Every reference to the back buffers must be gone before ResizeBuffers.
  context_->OMSetRenderTargets(0, nullptr, nullptr);
  renderTarget_.Reset();
  const HRESULT hr = swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
  if (FAILED(hr)) {
    return std::unexpected(failure("IDXGISwapChain::ResizeBuffers", hr));
  }
  return createRenderTarget();
}

void D3d11Device::beginFrame(const std::array<float, 4>& clearRgba) {
  ID3D11RenderTargetView* target = renderTarget_.Get();
  context_->OMSetRenderTargets(1, &target, nullptr);
  context_->ClearRenderTargetView(target, clearRgba.data());
}

D3d11Device::PresentResult D3d11Device::present() {
  const HRESULT hr = swapChain_->Present(syncInterval_, 0);
  if (hr == DXGI_STATUS_OCCLUDED) {
    return PresentResult::Occluded;
  }
  if (FAILED(hr)) {
    return PresentResult::DeviceLost;
  }
  return PresentResult::Shown;
}

bool D3d11Device::stillOccluded() {
  return swapChain_->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED;
}

#ifdef BITOSCOPE_GUI_TEST_HOOK
std::expected<D3d11Device::CapturedImage, std::string> D3d11Device::captureBackBuffer() {
  Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
  HRESULT hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
  if (FAILED(hr)) {
    return std::unexpected(failure("IDXGISwapChain::GetBuffer", hr));
  }
  D3D11_TEXTURE2D_DESC desc{};
  backBuffer->GetDesc(&desc);
  if (desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM) {
    return std::unexpected(std::format("the back buffer's format ({}) is not RGBA8", static_cast<int>(desc.Format)));
  }
  D3D11_TEXTURE2D_DESC stagingDesc = desc;
  stagingDesc.Usage = D3D11_USAGE_STAGING;
  stagingDesc.BindFlags = 0;
  stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  stagingDesc.MiscFlags = 0;
  stagingDesc.SampleDesc.Count = 1;
  stagingDesc.SampleDesc.Quality = 0;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
  hr = device_->CreateTexture2D(&stagingDesc, nullptr, &staging);
  if (FAILED(hr)) {
    return std::unexpected(failure("ID3D11Device::CreateTexture2D(staging)", hr));
  }
  context_->CopyResource(staging.Get(), backBuffer.Get());
  D3D11_MAPPED_SUBRESOURCE mapped{};
  hr = context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  if (FAILED(hr)) {
    return std::unexpected(failure("ID3D11DeviceContext::Map", hr));
  }
  CapturedImage image;
  image.width = desc.Width;
  image.height = desc.Height;
  image.rgba.resize(static_cast<std::size_t>(desc.Width) * desc.Height * 4);
  const auto* source = static_cast<const std::uint8_t*>(mapped.pData);
  for (UINT y = 0; y < desc.Height; ++y) {
    std::uint8_t* row = image.rgba.data() + static_cast<std::size_t>(y) * desc.Width * 4;
    std::memcpy(row, source + static_cast<std::size_t>(y) * mapped.RowPitch, static_cast<std::size_t>(desc.Width) * 4);
    for (UINT x = 0; x < desc.Width; ++x) {
      row[x * 4 + 3] = 255;
    }
  }
  context_->Unmap(staging.Get(), 0);
  return image;
}
#endif

}
