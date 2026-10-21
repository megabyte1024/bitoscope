/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "waveform_renderer.h"

namespace bitoscope::gui::win32 {

class D3d11WaveformRenderer final : public WaveformRenderer {
 public:
  D3d11WaveformRenderer(ID3D11Device* device, ID3D11DeviceContext* context);

  void upload(std::uint64_t view, std::uint32_t width, std::uint32_t rows,
              std::span<const std::uint8_t> levels) override;
  bool holds(std::uint64_t view, std::uint32_t width, std::uint32_t rows) const override;
  void draw(ImDrawList* drawList, std::uint64_t view, float left, std::span<const Lane> lanes,
            float lineWidth) override;
  void release(std::uint64_t view) override;
  std::vector<std::string> takeProblems() override;

 private:
  struct View {
    std::uint32_t width = 0;
    std::uint32_t rows = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> levels;
    float left = 0;
    float lineWidth = 1;
    std::vector<Lane> lanes;
  };
  struct Payload {
    D3d11WaveformRenderer* renderer;
    std::uint64_t view;
  };

  static void callback(const ImDrawList* list, const ImDrawCmd* cmd);
  void render(std::uint64_t view, const ImDrawCmd* cmd);
  bool ensurePipeline();
  void problem(std::string line);

  ID3D11Device* device_;
  ID3D11DeviceContext* context_;
  bool pipelineTried_ = false;
  bool pipelineReady_ = false;
  Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader_;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
  Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_;
  Microsoft::WRL::ComPtr<ID3D11BlendState> blend_;
  Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_;
  std::map<std::uint64_t, View> views_;
  std::vector<std::string> problems_;
};

}
