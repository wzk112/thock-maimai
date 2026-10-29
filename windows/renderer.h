#pragma once
#include "core.h"
#include "assets.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <memory>
template<class T> using Com=Microsoft::WRL::ComPtr<T>;
void hrCheck(HRESULT hr,const char* operation);
struct alignas(16) Parameters {float geometry[4]{},tint[4]{},frame[4]{},options[4]{};};
struct FrameProof {float peak=0,cornerAlpha=0;size_t visible=0;};
class Renderer {
    Com<ID3D11Device> device; Com<ID3D11DeviceContext> context;
    Com<ID3D11VertexShader> spriteVS,fullVS;
    Com<ID3D11PixelShader> spritePS,rainbowPS,finishPS;
    Com<ID3D11Buffer> constants;
    Com<ID3D11SamplerState> sampler;
    Com<ID3D11BlendState> alphaBlend,addBlend,screenBlend;
    Com<ID3D11RasterizerState> raster;
    std::map<std::string,Com<ID3D11ShaderResourceView>> textures;
    void update(const Parameters& p);
    void scene(ID3D11RenderTargetView* target,int width,int height,const Simulation& sim,double now);
    void finish(ID3D11RenderTargetView* target,ID3D11ShaderResourceView* source,int w,int h,float range,float white);
public:
    Renderer(const Assets& assets,const Simulation& sim,bool forceWarp=false);
    struct Overlay {
        HWND hwnd=nullptr;HMONITOR monitor=nullptr;int width=0,height=0;
        Com<IDXGISwapChain1> swap; Com<IDCompositionDevice> composition;
        Com<IDCompositionTarget> target;Com<IDCompositionVisual> visual;
        Com<ID3D11RenderTargetView> output,intermediate;
        Com<ID3D11ShaderResourceView> source;
        ~Overlay(){if(hwnd)DestroyWindow(hwnd);}
    };
    std::unique_ptr<Overlay> createOverlay(HINSTANCE instance,HMONITOR monitor);
    void draw(Overlay& o,const Simulation& sim,double now,bool hdr);
    FrameProof proof(const Simulation& sim,double now,float headroom,const fs::path& png);
};
