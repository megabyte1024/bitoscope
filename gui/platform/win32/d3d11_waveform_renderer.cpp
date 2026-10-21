/*
 * SPDX-License-Identifier: MIT
 */

#include "d3d11_waveform_renderer.h"

#include <d3dcompiler.h>

#include <cmath>
#include <cstring>
#include <format>

namespace bitoscope::gui::win32 {

namespace {

struct LaneConstants {
  float band[4];
  float highColor[4];
  float lowColor[4];
  float edgeColor[4];
  float fillColor[4];
  float outline[4];
  float display[2];
  float lineWidth;
  float row;
};
static_assert(sizeof(LaneConstants) % 16 == 0);

constexpr std::uint32_t kMaxTextureEdge = 8192;

// WHY: Hand-transcribed from waveform.frag and classifyLanePixel() in waveform_pixel.h: keep all three in step.
constexpr char kShaderSource[] = R"(
cbuffer LaneConstants : register(b0) {
  float4 band;       // left, top, right, bottom (framebuffer pixels)
  float4 highColor;  // a High column's line
  float4 lowColor;   // a Low column's line
  float4 edgeColor;  // a transition/level-change column
  float4 fillColor;  // a == 0: no fill under a High column (step W19)
  float4 outline;    // rgb: the outline colour; a: > 0.5 means this lane draws one
  float2 display;
  float lineWidth;
  float row;
};

Texture2D<uint> levels : register(t0);

struct VsOut {
  float4 pos : SV_POSITION;
};

VsOut vs_main(uint id : SV_VertexID) {
  // A triangle strip over the band: (0,0) (1,0) (0,1) (1,1).
  float2 corner = float2(id & 1, id >> 1);
  float2 p = band.xy + (band.zw - band.xy) * corner;
  VsOut o;
  o.pos = float4(p.x / display.x * 2.0 - 1.0, 1.0 - p.y / display.y * 2.0, 0.0, 1.0);
  return o;
}

// isFullHeight() (libs/waveform_view/waveform_pixel.h): `at` is a transition, or the level
// genuinely changes between `before` and `at` (both a real level, 0 or 1).
bool isFullHeightAt(uint at, bool hasBefore, uint before) {
  if (at == 2) return true;
  if (!hasBefore) return false;
  return at <= 1 && before <= 1 && before != at;
}

float4 ps_main(VsOut i) : SV_Target {
  uint width, height;
  levels.GetDimensions(width, height);
  int col = (int)floor(i.pos.x - band.x);
  int r = (int)row;
  if (col < 0 || col >= (int)width) discard;
  uint here = levels.Load(int3(col, r, 0));
  if (here == 3) discard;               // no data here
  bool hasOutline = outline.a > 0.5;

  bool hasBefore = col > 0;
  uint before = hasBefore ? levels.Load(int3(col - 1, r, 0)) : 0;
  bool fullHeight = isFullHeightAt(here, hasBefore, before);
  if (fullHeight) {
    if (hasOutline && ((i.pos.y - band.y) < 1.0 || (band.w - i.pos.y) < 1.0)) {
      return float4(outline.rgb, 1.0);  // the band's own top/bottom rim
    }
    return edgeColor;
  }

  // Fix (b): an outlined lane's line sits one physical pixel further IN from the band edge, so its
  // own outer rim (below) lands inside the band instead of one row outside it
  // (libs/waveform_view/waveform_pixel.h's own comment).
  float halfWidth = lineWidth * 0.5;
  float inset = hasOutline ? 1.0 : 0.0;
  float lineY = here == 1 ? band.y + halfWidth + inset : band.w - halfWidth - inset;
  float4 lineColor = here == 1 ? highColor : lowColor;
  float dist = abs(i.pos.y - lineY);

  // Fix (a): this column is not itself full height, but sits immediately beside one - every one of
  // ITS pixels that is not its own line pixel becomes the vertical's side rim, along the column's
  // whole height.
  if (hasOutline && dist > halfWidth) {
    bool hasBefore2 = col > 1;
    uint before2 = hasBefore2 ? levels.Load(int3(col - 2, r, 0)) : 0;
    bool leftIsFullHeight = hasBefore && isFullHeightAt(before, hasBefore2, before2);
    bool hasAfter = col + 1 < (int)width;
    uint after = hasAfter ? levels.Load(int3(col + 1, r, 0)) : 0;
    bool rightIsFullHeight = hasAfter && isFullHeightAt(after, true, here);
    if (leftIsFullHeight || rightIsFullHeight) {
      return float4(outline.rgb, 1.0);
    }
  }

  if (dist <= halfWidth) return lineColor;
  if (hasOutline && dist <= halfWidth + 1.0) return float4(outline.rgb, 1.0);
  if (here == 1 && fillColor.a > 0.0) return fillColor;  // step W19, blended (see below)
  discard;
  return lineColor;
}
)";

std::string failure(const char* what, HRESULT hr) {
  return std::format("Waveform pass: {} failed (HRESULT 0x{:08X})", what, static_cast<unsigned long>(hr));
}

}

D3d11WaveformRenderer::D3d11WaveformRenderer(ID3D11Device* device, ID3D11DeviceContext* context)
    : device_(device), context_(context) {}

void D3d11WaveformRenderer::problem(std::string line) {
  if (problems_.empty() || problems_.back() != line) {
    problems_.push_back(std::move(line));
  }
}

std::vector<std::string> D3d11WaveformRenderer::takeProblems() {
  std::vector<std::string> out;
  out.swap(problems_);
  return out;
}

bool D3d11WaveformRenderer::ensurePipeline() {
  if (pipelineTried_) {
    return pipelineReady_;
  }
  pipelineTried_ = true;
  const auto compile = [&](const char* entry, const char* target,
                           Microsoft::WRL::ComPtr<ID3DBlob>& blob) -> bool {
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompile(kShaderSource, sizeof(kShaderSource) - 1, "waveform", nullptr, nullptr, entry,
                                  target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors);
    if (FAILED(hr)) {
      std::string text = errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()),
                                              errors->GetBufferSize())
                                : std::string();
      problem(std::format("{}: {}", failure("compiling shader", hr), text));
      return false;
    }
    return true;
  };
  Microsoft::WRL::ComPtr<ID3DBlob> vs;
  Microsoft::WRL::ComPtr<ID3DBlob> ps;
  if (!compile("vs_main", "vs_4_0", vs) || !compile("ps_main", "ps_4_0", ps)) {
    return false;
  }
  HRESULT hr = device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertexShader_);
  if (FAILED(hr)) {
    problem(failure("CreateVertexShader", hr));
    return false;
  }
  hr = device_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &pixelShader_);
  if (FAILED(hr)) {
    problem(failure("CreatePixelShader", hr));
    return false;
  }
  D3D11_BUFFER_DESC cb{};
  cb.ByteWidth = sizeof(LaneConstants);
  cb.Usage = D3D11_USAGE_DYNAMIC;
  cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  if (hr = device_->CreateBuffer(&cb, nullptr, &constants_); FAILED(hr)) {
    problem(failure("CreateBuffer (constants)", hr));
    return false;
  }
  D3D11_RASTERIZER_DESC rs{};
  rs.FillMode = D3D11_FILL_SOLID;
  rs.CullMode = D3D11_CULL_NONE;
  rs.ScissorEnable = TRUE;
  rs.DepthClipEnable = TRUE;
  if (hr = device_->CreateRasterizerState(&rs, &rasterizer_); FAILED(hr)) {
    problem(failure("CreateRasterizerState", hr));
    return false;
  }
  // WHY: Standard alpha blending: the high-level fill has a real alpha, every other colour is written with alpha 1.0.
  D3D11_BLEND_DESC bs{};
  bs.RenderTarget[0].BlendEnable = TRUE;
  bs.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
  bs.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  bs.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
  bs.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
  bs.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
  bs.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
  bs.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  if (hr = device_->CreateBlendState(&bs, &blend_); FAILED(hr)) {
    problem(failure("CreateBlendState", hr));
    return false;
  }
  D3D11_DEPTH_STENCIL_DESC ds{};
  ds.DepthEnable = FALSE;
  ds.StencilEnable = FALSE;
  if (hr = device_->CreateDepthStencilState(&ds, &depth_); FAILED(hr)) {
    problem(failure("CreateDepthStencilState", hr));
    return false;
  }
  pipelineReady_ = true;
  return true;
}

void D3d11WaveformRenderer::upload(std::uint64_t view, std::uint32_t width, std::uint32_t rows,
                                   std::span<const std::uint8_t> levels) {
  View& v = views_[view];
  if (width == 0 || rows == 0 || levels.size() != static_cast<std::size_t>(width) * rows) {
    v = View{};
    return;
  }
  if (width > kMaxTextureEdge || rows > kMaxTextureEdge) {
    v = View{};
    problem(std::format("Waveform pass: view of {} x {} columns and rows is larger than "
                        "texture may be ({})", width, rows, kMaxTextureEdge));
    return;
  }
  if (v.texture && v.width == width && v.rows == rows) {
    context_->UpdateSubresource(v.texture.Get(), 0, nullptr, levels.data(), width, 0);
    return;
  }
  v.texture.Reset();
  v.levels.Reset();
  v.width = v.rows = 0;
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = width;
  desc.Height = rows;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8_UINT;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA data{};
  data.pSysMem = levels.data();
  data.SysMemPitch = width;
  HRESULT hr = device_->CreateTexture2D(&desc, &data, &v.texture);
  if (FAILED(hr)) {
    problem(failure("CreateTexture2D", hr));
    return;
  }
  hr = device_->CreateShaderResourceView(v.texture.Get(), nullptr, &v.levels);
  if (FAILED(hr)) {
    v.texture.Reset();
    problem(failure("CreateShaderResourceView", hr));
    return;
  }
  v.width = width;
  v.rows = rows;
}

bool D3d11WaveformRenderer::holds(std::uint64_t view, std::uint32_t width, std::uint32_t rows) const {
  const auto it = views_.find(view);
  return it != views_.end() && it->second.levels && it->second.width == width && it->second.rows == rows;
}

void D3d11WaveformRenderer::draw(ImDrawList* drawList, std::uint64_t view, float left,
                                 std::span<const Lane> lanes, float lineWidth) {
  const auto it = views_.find(view);
  if (it == views_.end() || !it->second.levels || lanes.empty()) {
    return;
  }
  it->second.left = left;
  it->second.lineWidth = lineWidth;
  it->second.lanes.assign(lanes.begin(), lanes.end());
  Payload payload{this, view};
  drawList->AddCallback(&D3d11WaveformRenderer::callback, &payload, sizeof(payload));
  drawList->AddCallback(ImGui::GetPlatformIO().DrawCallback_ResetRenderState, nullptr);
}

void D3d11WaveformRenderer::callback(const ImDrawList*, const ImDrawCmd* cmd) {
  Payload payload;
  std::memcpy(&payload, cmd->UserCallbackData, sizeof(payload));
  payload.renderer->render(payload.view, cmd);
}

void D3d11WaveformRenderer::render(std::uint64_t view, const ImDrawCmd* cmd) {
  const auto it = views_.find(view);
  if (it == views_.end() || !it->second.levels || !ensurePipeline()) {
    return;
  }
  const View& v = it->second;
  const ImDrawData* drawData = ImGui::GetDrawData();
  if (drawData == nullptr) {
    return;
  }
  const ImVec2 origin = drawData->DisplayPos;
  const ImVec2 scale = drawData->FramebufferScale;
  const float displayWidth = drawData->DisplaySize.x * scale.x;
  const float displayHeight = drawData->DisplaySize.y * scale.y;
  const D3D11_RECT scissor{static_cast<LONG>((cmd->ClipRect.x - origin.x) * scale.x),
                           static_cast<LONG>((cmd->ClipRect.y - origin.y) * scale.y),
                           static_cast<LONG>((cmd->ClipRect.z - origin.x) * scale.x),
                           static_cast<LONG>((cmd->ClipRect.w - origin.y) * scale.y)};
  if (scissor.right <= scissor.left || scissor.bottom <= scissor.top || displayWidth <= 0 ||
      displayHeight <= 0) {
    return;
  }

  context_->RSSetScissorRects(1, &scissor);
  context_->RSSetState(rasterizer_.Get());
  const float blendFactor[4] = {0, 0, 0, 0};
  context_->OMSetBlendState(blend_.Get(), blendFactor, 0xffffffff);
  context_->OMSetDepthStencilState(depth_.Get(), 0);
  context_->IASetInputLayout(nullptr);
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  context_->VSSetShader(vertexShader_.Get(), nullptr, 0);
  context_->PSSetShader(pixelShader_.Get(), nullptr, 0);
  context_->GSSetShader(nullptr, nullptr, 0);
  ID3D11Buffer* constants = constants_.Get();
  context_->VSSetConstantBuffers(0, 1, &constants);
  context_->PSSetConstantBuffers(0, 1, &constants);
  ID3D11ShaderResourceView* levels = v.levels.Get();
  context_->PSSetShaderResources(0, 1, &levels);

  const float left = std::round((v.left - origin.x) * scale.x);
  const float lineWidth = (std::max)(1.0f, std::round(v.lineWidth * scale.y));
  for (const Lane& lane : v.lanes) {
    if (lane.row >= v.rows) {
      continue;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT hr = context_->Map(constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
      problem(failure("Map (lane constants)", hr));
      return;
    }
    LaneConstants c{};
    c.band[0] = left;
    c.band[1] = std::round((lane.top - origin.y) * scale.y);
    c.band[2] = left + static_cast<float>(v.width);
    c.band[3] = std::round((lane.bottom - origin.y) * scale.y);
    std::memcpy(c.highColor, lane.highColor.data(), sizeof(c.highColor));
    std::memcpy(c.lowColor, lane.lowColor.data(), sizeof(c.lowColor));
    std::memcpy(c.edgeColor, lane.edgeColor.data(), sizeof(c.edgeColor));
    std::memcpy(c.fillColor, lane.fillColor.data(), sizeof(c.fillColor));
    std::memcpy(c.outline, lane.outlineColor.data(), sizeof(c.outline));
    c.outline[3] = lane.needsOutline ? 1.0f : 0.0f;
    c.display[0] = displayWidth;
    c.display[1] = displayHeight;
    c.lineWidth = lineWidth;
    c.row = static_cast<float>(lane.row);
    std::memcpy(mapped.pData, &c, sizeof(c));
    context_->Unmap(constants, 0);
    context_->Draw(4, 0);
  }
}

void D3d11WaveformRenderer::release(std::uint64_t view) {
  views_.erase(view);
}

}
