#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_3.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <imgui.h>

using Microsoft::WRL::ComPtr;

class Renderer {
public:
    struct VS_ConstantBuffer {
        float scale[4];
        float translate[4];
        float windowSize[4];
    };

    struct PS_Field_ConstantBuffer {
        float color[4];
    };

    struct PS_Circle_ConstantBuffer {
        float color[4];
        float center[2];
        float radius;
        float featherWidth;
    };

    struct FieldCache {
        float fieldSize = 0.0f;
        ImVec2 fieldTL = ImVec2(0, 0);
        ImVec2 fieldBR = ImVec2(0, 0);
        ImVec2 center = ImVec2(0, 0);
        float halfField = 0.0f;
        float circleRadiusPx = 0.0f;
        float spawnMaxRadius = 0.0f;
        float cursorRadiusPx = 0.0f;
        bool valid = false;
    };

    Renderer();
    ~Renderer();

    bool Initialize(HWND hWnd, int width, int height, int refreshRate);
    void SetMaxFrameLatency(UINT latency);
    void Cleanup();
    void Resize(int width, int height);

    void BeginFrame(const float clearColor[4]);
    void EndFrame();

    void DrawField(const FieldCache& fieldCache, const ImVec4& fieldColor);
    void BeginCircleRendering();
    void DrawCircle(const ImVec2& center, float radius, const ImVec4& color, float feather = 1.0f);
    void EndCircleRendering();
    void UpdateFieldCache(FieldCache& cache, float scale, float circleRadiusNorm, float cursorRadiusNorm);
    void WaitForFrameLatencyObject();

    ID3D11Device* GetDevice() { return m_pd3dDevice.Get(); }
    ID3D11DeviceContext* GetDeviceContext() { return m_pd3dDeviceContext.Get(); }

private:
    VS_ConstantBuffer m_lastVSData;
    PS_Field_ConstantBuffer m_lastPSFieldData;
    PS_Circle_ConstantBuffer m_lastPSCircleData;
    bool m_hasLastVSData = false;
    bool m_hasLastPSFieldData = false;
    bool m_hasLastPSCircleData = false;

    bool InitGraphics();
    void CleanupRenderTarget();
    void CreateRenderTarget();

    void UpdateVSConstantBuffer(const VS_ConstantBuffer& data);
    void UpdatePSCircleConstantBuffer(const PS_Circle_ConstantBuffer& data);
    void UpdatePSFieldConstantBuffer(const PS_Field_ConstantBuffer& data);

    HWND m_hWnd;
    int m_width;
    int m_height;

    UINT m_maxFrameLatency = 1;
    HANDLE m_frameLatencyWaitableObject = NULL;

    ComPtr<ID3D11Device> m_pd3dDevice;
    ComPtr<ID3D11DeviceContext> m_pd3dDeviceContext;
    ComPtr<IDXGISwapChain> m_pSwapChain;
    ComPtr<ID3D11RenderTargetView> m_mainRenderTargetView;

    ComPtr<ID3D11BlendState> m_pBlendState;
    ComPtr<ID3D11DepthStencilState> m_pDepthStencilState;
    ComPtr<ID3D11RasterizerState> m_pRasterizerState;

    ComPtr<ID3D11VertexShader> m_pVS;
    ComPtr<ID3D11PixelShader> m_pPS_Field;
    ComPtr<ID3D11PixelShader> m_pPS_Circle;
    ComPtr<ID3D11InputLayout> m_pVertexLayout;
    ComPtr<ID3D11Buffer> m_pVertexBuffer;
    ComPtr<ID3D11Buffer> m_pIndexBuffer;
    ComPtr<ID3D11Buffer> m_pConstantBufferVS;
    ComPtr<ID3D11Buffer> m_pConstantBufferPS_Field;
    ComPtr<ID3D11Buffer> m_pConstantBufferPS_Circle;
};