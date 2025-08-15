#define NOMINMAX
#include "renderer.h"
#include <algorithm>

Renderer::Renderer()
    : m_hWnd(nullptr), m_width(0), m_height(0),
    m_hasLastVSData(false),
    m_hasLastPSFieldData(false),
    m_hasLastPSCircleData(false)
{
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ZeroMemory(&m_lastVSData, sizeof(m_lastVSData));
    ZeroMemory(&m_lastPSFieldData, sizeof(m_lastPSFieldData));
    ZeroMemory(&m_lastPSCircleData, sizeof(m_lastPSCircleData));
    m_frameLatencyWaitableObject = NULL;
}

Renderer::~Renderer() {
    Cleanup();
}

bool Renderer::Initialize(HWND hWnd, int width, int height, int refreshRate) {
    m_hWnd = hWnd;
    m_width = width;
    m_height = height;

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = width;
    sd.BufferDesc.Height = height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = refreshRate;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH |DXGI_PRESENT_DO_NOT_WAIT | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    UINT createDeviceFlags = 0;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        NULL,
        D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        createDeviceFlags,
        featureLevelArray,
        2,
        D3D11_SDK_VERSION,
        &sd,
        &m_pSwapChain,
        &m_pd3dDevice,
        &featureLevel,
        &m_pd3dDeviceContext
    );
    if (FAILED(hr)) return false;

    ComPtr<IDXGISwapChain2> swapChain2;
    if (SUCCEEDED(m_pSwapChain.As(&swapChain2))) {
        swapChain2->SetMaximumFrameLatency(m_maxFrameLatency);
        m_frameLatencyWaitableObject = swapChain2->GetFrameLatencyWaitableObject();
    }

    CreateRenderTarget();

    // Setup the viewport
    D3D11_VIEWPORT vp;
    vp.Width = (FLOAT)width;
    vp.Height = (FLOAT)height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    m_pd3dDeviceContext->RSSetViewports(1, &vp);

    // Create depth stencil state (disabled)
    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = false;
    dsDesc.StencilEnable = false;
    m_pd3dDevice->CreateDepthStencilState(&dsDesc, &m_pDepthStencilState);
    m_pd3dDeviceContext->OMSetDepthStencilState(m_pDepthStencilState.Get(), 0);

    // Create blend state for alpha blending
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = false;
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    m_pd3dDevice->CreateBlendState(&blendDesc, &m_pBlendState);
    m_pd3dDeviceContext->OMSetBlendState(m_pBlendState.Get(), nullptr, 0xFFFFFFFF);

    // Create rasterizer state
    D3D11_RASTERIZER_DESC rsDesc = {};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_NONE;
    rsDesc.ScissorEnable = false;
    rsDesc.DepthClipEnable = false;
    m_pd3dDevice->CreateRasterizerState(&rsDesc, &m_pRasterizerState);
    m_pd3dDeviceContext->RSSetState(m_pRasterizerState.Get());

    return InitGraphics();
}

void Renderer::SetMaxFrameLatency(UINT latency) {
    if (latency < 1) latency = 1;
    m_maxFrameLatency = latency;

    ComPtr<IDXGISwapChain2> swapChain2;
    if (SUCCEEDED(m_pSwapChain.As(&swapChain2))) {
        swapChain2->SetMaximumFrameLatency(latency);
    }
}

void Renderer::Cleanup() {
    if (m_pSwapChain) m_pSwapChain->SetFullscreenState(FALSE, NULL);
    CleanupRenderTarget();

    m_pConstantBufferPS_Circle.Reset();
    m_pConstantBufferPS_Field.Reset();
    m_pConstantBufferVS.Reset();
    m_pIndexBuffer.Reset();
    m_pVertexBuffer.Reset();
    m_pVertexLayout.Reset();
    m_pPS_Circle.Reset();
    m_pPS_Field.Reset();
    m_pVS.Reset();

    m_pRasterizerState.Reset();
    m_pBlendState.Reset();
    m_pDepthStencilState.Reset();

    m_pSwapChain.Reset();
    m_pd3dDeviceContext.Reset();
    m_pd3dDevice.Reset();
}

void Renderer::Resize(int width, int height) {
    if (m_pd3dDevice == nullptr) return;

    m_width = width;
    m_height = height;

    CleanupRenderTarget();
    m_pSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    CreateRenderTarget();

    // Update viewport
    D3D11_VIEWPORT vp;
    vp.Width = (FLOAT)width;
    vp.Height = (FLOAT)height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    m_pd3dDeviceContext->RSSetViewports(1, &vp);

    m_hasLastVSData = false;
}

void Renderer::BeginFrame(const float clearColor[4]) {
    m_pd3dDeviceContext->ClearRenderTargetView(m_mainRenderTargetView.Get(), clearColor);
    m_pd3dDeviceContext->OMSetRenderTargets(1, m_mainRenderTargetView.GetAddressOf(), nullptr);
    m_pd3dDeviceContext->RSSetState(m_pRasterizerState.Get());

    // Set common resources for all objects
    UINT stride = sizeof(float) * 2;
    UINT offset = 0;
    m_pd3dDeviceContext->IASetVertexBuffers(0, 1, m_pVertexBuffer.GetAddressOf(), &stride, &offset);
    m_pd3dDeviceContext->IASetIndexBuffer(m_pIndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
    m_pd3dDeviceContext->IASetInputLayout(m_pVertexLayout.Get());
    m_pd3dDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Renderer::EndFrame() {
    m_pSwapChain->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
}

void Renderer::DrawField(const FieldCache& fieldCache, const ImVec4& fieldColor) {
    // Set shaders for field
    m_pd3dDeviceContext->VSSetShader(m_pVS.Get(), nullptr, 0);
    m_pd3dDeviceContext->VSSetConstantBuffers(0, 1, m_pConstantBufferVS.GetAddressOf());
    m_pd3dDeviceContext->PSSetShader(m_pPS_Field.Get(), nullptr, 0);
    m_pd3dDeviceContext->PSSetConstantBuffers(0, 1, m_pConstantBufferPS_Field.GetAddressOf());

    // Prepare buffer data
    VS_ConstantBuffer vsConst = {};
    vsConst.scale[0] = fieldCache.fieldSize;
    vsConst.scale[1] = fieldCache.fieldSize;
    vsConst.translate[0] = fieldCache.fieldTL.x;
    vsConst.translate[1] = fieldCache.fieldTL.y;
    vsConst.windowSize[0] = (float)m_width;
    vsConst.windowSize[1] = (float)m_height;

    PS_Field_ConstantBuffer psFieldConst = {};
    memcpy(psFieldConst.color, &fieldColor, 4 * sizeof(float));

    // Check for VS changes
    if (!m_hasLastVSData ||
        vsConst.scale[0] != m_lastVSData.scale[0] ||
        vsConst.scale[1] != m_lastVSData.scale[1] ||
        vsConst.translate[0] != m_lastVSData.translate[0] ||
        vsConst.translate[1] != m_lastVSData.translate[1] ||
        vsConst.windowSize[0] != m_lastVSData.windowSize[0] ||
        vsConst.windowSize[1] != m_lastVSData.windowSize[1])
    {
        UpdateVSConstantBuffer(vsConst);
        m_lastVSData = vsConst;
        m_hasLastVSData = true;
    }

    if (!m_hasLastPSFieldData ||
        memcmp(&psFieldConst, &m_lastPSFieldData, sizeof(PS_Field_ConstantBuffer)) != 0)
    {
        UpdatePSFieldConstantBuffer(psFieldConst);
        m_lastPSFieldData = psFieldConst;
        m_hasLastPSFieldData = true;
    }

    // Draw field
    m_pd3dDeviceContext->DrawIndexed(6, 0, 0);
}

void Renderer::BeginCircleRendering() {
    m_pd3dDeviceContext->VSSetShader(m_pVS.Get(), nullptr, 0);
    m_pd3dDeviceContext->PSSetShader(m_pPS_Circle.Get(), nullptr, 0);
    m_pd3dDeviceContext->VSSetConstantBuffers(0, 1, m_pConstantBufferVS.GetAddressOf());
    m_pd3dDeviceContext->PSSetConstantBuffers(0, 1, m_pConstantBufferPS_Circle.GetAddressOf());
}

void Renderer::DrawCircle(const ImVec2& center, float radius, const ImVec4& color, float feather) {
    // Prepare VS
    VS_ConstantBuffer vsConst = {};
    vsConst.scale[0] = radius * 2.0f;
    vsConst.scale[1] = radius * 2.0f;
    vsConst.translate[0] = center.x - radius;
    vsConst.translate[1] = center.y - radius;
    vsConst.windowSize[0] = (float)m_width;
    vsConst.windowSize[1] = (float)m_height;

    if (!m_hasLastVSData || memcmp(&vsConst, &m_lastVSData, sizeof(VS_ConstantBuffer)) != 0) {
        UpdateVSConstantBuffer(vsConst);
        m_lastVSData = vsConst;
        m_hasLastVSData = true;
    }

    // Prepare PS
    PS_Circle_ConstantBuffer psCircleConst = {};
    memcpy(psCircleConst.color, &color, 4 * sizeof(float));
    psCircleConst.center[0] = center.x;
    psCircleConst.center[1] = center.y;
    psCircleConst.radius = radius;
    psCircleConst.featherWidth = feather;

    if (!m_hasLastPSCircleData || memcmp(&psCircleConst, &m_lastPSCircleData, sizeof(PS_Circle_ConstantBuffer)) != 0) {
        UpdatePSCircleConstantBuffer(psCircleConst);
        m_lastPSCircleData = psCircleConst;
        m_hasLastPSCircleData = true;
    }

    m_pd3dDeviceContext->DrawIndexed(6, 0, 0);
}

void Renderer::EndCircleRendering() {
    m_hasLastPSCircleData = false;
}

void Renderer::UpdateFieldCache(FieldCache& cache, float scale, float circleRadiusNorm, float cursorRadiusNorm) {
    float screenW = static_cast<float>(m_width);
    float screenH = static_cast<float>(m_height);

    if (screenW <= 0 || screenH <= 0) {
        cache.valid = false;
        return;
    }

    float fieldSize = std::min(screenW, screenH) * scale;
    float halfField = fieldSize * 0.5f;
    ImVec2 fieldTL{ (screenW - fieldSize) * 0.5f, (screenH - fieldSize) * 0.5f };
    ImVec2 fieldBR{ fieldTL.x + fieldSize, fieldTL.y + fieldSize };
    ImVec2 center{ fieldTL.x + halfField, fieldTL.y + halfField };
    float circleRadiusPx = circleRadiusNorm * halfField;
    float spawnMaxRadius = std::max(0.0f, halfField - circleRadiusPx);

    cache.fieldSize = fieldSize;
    cache.halfField = halfField;
    cache.fieldTL = fieldTL;
    cache.fieldBR = fieldBR;
    cache.center = center;
    cache.circleRadiusPx = circleRadiusPx;
    cache.spawnMaxRadius = spawnMaxRadius;
    cache.cursorRadiusPx = cursorRadiusNorm * halfField;
    cache.valid = true;
}

bool Renderer::InitGraphics() {
    HRESULT hr;
    ComPtr<ID3DBlob> pVSBlob, pPSBlob, errorBlob;

    // Vertex shader
    const char* vsCode = R"(
    cbuffer Transform : register(b0)
    {
        float4 scale;        
        float4 translate;    
        float4 windowSize;   
    };
    struct VS_INPUT
    {
        float2 pos : POSITION;
    };
    struct VS_OUTPUT
    {
        float4 pos : SV_POSITION;
        float2 worldPos : TEXCOORD0;
    };
    VS_OUTPUT main(VS_INPUT input)
    {
        VS_OUTPUT output;
        float2 worldPos = input.pos * scale.xy + translate.xy;
        output.worldPos = worldPos;
        output.pos = float4(
            (worldPos.x / windowSize.x) * 2.0 - 1.0,
            (worldPos.y / windowSize.y) * -2.0 + 1.0,
            0.0, 1.0);
        return output;
    })";

    hr = D3DCompile(vsCode, strlen(vsCode), nullptr, nullptr, nullptr, "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pVSBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            errorBlob->Release();
        }
        return false;
    }
    hr = m_pd3dDevice->CreateVertexShader(pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), nullptr, &m_pVS);
    if (FAILED(hr)) return false;

    // Pixel shader for field
    const char* psFieldCode = R"(
    cbuffer PS_Field : register(b0)
    {
        float4 color;
    };
    float4 main() : SV_Target
    {
        return color;
    })";

    hr = D3DCompile(psFieldCode, strlen(psFieldCode), nullptr, nullptr, nullptr, "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pPSBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            errorBlob->Release();
        }
        return false;
    }
    hr = m_pd3dDevice->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), nullptr, &m_pPS_Field);
    if (FAILED(hr)) return false;
    pPSBlob.Reset();

    // Pixel shader for circle
    const char* psCircleCode = R"(
    cbuffer PS_Circle : register(b0) {
        float4 color;
        float2 center;
        float radius;
        float featherWidth;
    };

    float4 main(float4 pos : SV_POSITION, float2 worldPos : TEXCOORD0) : SV_Target {
        float2 delta = worldPos - center;
        float dist = length(delta);
        float alpha = saturate( (radius - dist) / featherWidth );
        return float4(color.rgb, color.a * alpha);
    })";

    hr = D3DCompile(psCircleCode, strlen(psCircleCode), nullptr, nullptr, nullptr, "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pPSBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            errorBlob->Release();
        }
        return false;
    }
    hr = m_pd3dDevice->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), nullptr, &m_pPS_Circle);
    if (FAILED(hr)) return false;

    // Create input layout
    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    UINT numElements = ARRAYSIZE(layout);
    hr = m_pd3dDevice->CreateInputLayout(layout, numElements, pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), &m_pVertexLayout);
    if (FAILED(hr)) return false;

    // Vertex buffer 
    float vertices[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 1.0f
    };
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.ByteWidth = sizeof(vertices);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA initData = { vertices };
    hr = m_pd3dDevice->CreateBuffer(&bd, &initData, &m_pVertexBuffer);
    if (FAILED(hr)) return false;

    // Index buffer 
    unsigned short indices[] = { 0, 1, 2, 0, 2, 3 };
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.ByteWidth = sizeof(indices);
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    initData.pSysMem = indices;
    hr = m_pd3dDevice->CreateBuffer(&bd, &initData, &m_pIndexBuffer);
    if (FAILED(hr)) return false;

    // Constant buffers
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    // VS_ConstantBuffer
    bd.ByteWidth = sizeof(VS_ConstantBuffer);
    hr = m_pd3dDevice->CreateBuffer(&bd, nullptr, &m_pConstantBufferVS);
    if (FAILED(hr)) return false;

    // PS_Field_ConstantBuffer
    bd.ByteWidth = sizeof(PS_Field_ConstantBuffer);
    hr = m_pd3dDevice->CreateBuffer(&bd, nullptr, &m_pConstantBufferPS_Field);
    if (FAILED(hr)) return false;

    // PS_Circle_ConstantBuffer
    bd.ByteWidth = sizeof(PS_Circle_ConstantBuffer);
    hr = m_pd3dDevice->CreateBuffer(&bd, nullptr, &m_pConstantBufferPS_Circle);
    if (FAILED(hr)) return false;

    return true;
}

void Renderer::CleanupRenderTarget() {
    if (m_mainRenderTargetView) m_mainRenderTargetView.Reset();
}

void Renderer::CreateRenderTarget() {
    ComPtr<ID3D11Texture2D> pBackBuffer;
    m_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    m_pd3dDevice->CreateRenderTargetView(pBackBuffer.Get(), NULL, &m_mainRenderTargetView);
}

void Renderer::UpdateVSConstantBuffer(const VS_ConstantBuffer& data) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_pd3dDeviceContext->Map(m_pConstantBufferVS.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, &data, sizeof(VS_ConstantBuffer));
        m_pd3dDeviceContext->Unmap(m_pConstantBufferVS.Get(), 0);
    }
}

void Renderer::UpdatePSCircleConstantBuffer(const PS_Circle_ConstantBuffer& data) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_pd3dDeviceContext->Map(m_pConstantBufferPS_Circle.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, &data, sizeof(PS_Circle_ConstantBuffer));
        m_pd3dDeviceContext->Unmap(m_pConstantBufferPS_Circle.Get(), 0);
    }
}

void Renderer::UpdatePSFieldConstantBuffer(const PS_Field_ConstantBuffer& data) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_pd3dDeviceContext->Map(m_pConstantBufferPS_Field.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, &data, sizeof(PS_Field_ConstantBuffer));
        m_pd3dDeviceContext->Unmap(m_pConstantBufferPS_Field.Get(), 0);
    }
}

void Renderer::WaitForFrameLatencyObject() {
    if (m_frameLatencyWaitableObject) {
        WaitForSingleObjectEx(m_frameLatencyWaitableObject, 1000, TRUE);
    }
}