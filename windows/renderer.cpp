#include "renderer.h"
#include <d3dcompiler.h>
#include <wincodec.h>
#include <DirectXPackedVector.h>
#include <sstream>
void hrCheck(HRESULT hr,const char* op){if(FAILED(hr)){std::ostringstream s;s<<op<<" (0x"<<std::hex<<unsigned(hr)<<")";throw std::runtime_error(s.str());}}
static Com<ID3DBlob> compile(const std::string& source,const char* entry,const char* profile) {
    Com<ID3DBlob> code,error; HRESULT hr=D3DCompile(source.data(),source.size(),nullptr,nullptr,nullptr,entry,profile,
        D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
    if(FAILED(hr))throw std::runtime_error(error?std::string((char*)error->GetBufferPointer(),error->GetBufferSize()):"Shader compile failed");
    return code;
}
Renderer::Renderer(const fs::path& executable,const fs::path& assets,const Simulation& sim,bool warp) {
    auto flags=D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT result=D3D11CreateDevice(nullptr,warp?D3D_DRIVER_TYPE_WARP:D3D_DRIVER_TYPE_HARDWARE,nullptr,flags,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
    if(FAILED(result)&&!warp) result=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,flags,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
    hrCheck(result,"Create Direct3D device");
    auto source=readText(executable/"effect.hlsl");
    auto vs=compile(source,"SpriteVertex","vs_5_0"),fv=compile(source,"FullVertex","vs_5_0");
    auto ps=compile(source,"SpritePixel","ps_5_0"),fp=compile(source,"FinishPixel","ps_5_0");
    hrCheck(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&spriteVS),"Sprite vertex");
    hrCheck(device->CreateVertexShader(fv->GetBufferPointer(),fv->GetBufferSize(),nullptr,&fullVS),"Full vertex");
    hrCheck(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&spritePS),"Sprite pixel");
    hrCheck(device->CreatePixelShader(fp->GetBufferPointer(),fp->GetBufferSize(),nullptr,&finishPS),"Finish pixel");
    bool needsRainbow=false;for(auto& e:sim.emitters)if(e["material"]==49)needsRainbow=true;
    if(needsRainbow){auto r=compile(source+"\n"+readText(assets/"rainbow.hlsl"),"RainbowPixel","ps_5_0");
        hrCheck(device->CreatePixelShader(r->GetBufferPointer(),r->GetBufferSize(),nullptr,&rainbowPS),"Rainbow pixel");}
    D3D11_BUFFER_DESC cb{};cb.ByteWidth=sizeof(Parameters);cb.Usage=D3D11_USAGE_DEFAULT;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    hrCheck(device->CreateBuffer(&cb,nullptr,&constants),"Constants");
    D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
    hrCheck(device->CreateSamplerState(&sd,&sampler),"Sampler");
    for(int mode=0;mode<3;++mode){D3D11_BLEND_DESC b{};auto& r=b.RenderTarget[0];r.BlendEnable=TRUE;r.SrcBlend=D3D11_BLEND_ONE;
        r.DestBlend=mode==1?D3D11_BLEND_ONE:(mode==2?D3D11_BLEND_INV_SRC_COLOR:D3D11_BLEND_INV_SRC_ALPHA);
        r.BlendOp=D3D11_BLEND_OP_ADD;r.SrcBlendAlpha=D3D11_BLEND_ONE;r.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;r.BlendOpAlpha=D3D11_BLEND_OP_ADD;r.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        hrCheck(device->CreateBlendState(&b,mode==0?&alphaBlend:mode==1?&addBlend:&screenBlend),"Blend");}
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
    hrCheck(device->CreateRasterizerState(&rd,&raster),"Rasterizer");
    Com<IWICImagingFactory> wic;hrCheck(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)),"WIC");
    for(auto& e:sim.emitters) for(auto& n:e["textures"]){auto name=n.get<std::string>();if(textures.count(name))continue;
        auto path=safeFile(assets,name);if(fs::file_size(path)>16*1024*1024)throw std::runtime_error("Texture too large");
        Com<IWICBitmapDecoder> decoder;Com<IWICBitmapFrameDecode> frame;Com<IWICFormatConverter> converter;
        hrCheck(wic->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder),"Load PNG");
        hrCheck(decoder->GetFrame(0,&frame),"PNG frame");UINT width=0,height=0;frame->GetSize(&width,&height);
        if(!width||!height||width>4096||height>4096)throw std::runtime_error("Texture dimension limit");
        hrCheck(wic->CreateFormatConverter(&converter),"PNG convert");
        hrCheck(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"PNG RGBA");
        std::vector<uint8_t> data(size_t(width)*height*4);hrCheck(converter->CopyPixels(nullptr,width*4,UINT(data.size()),data.data()),"PNG pixels");
        D3D11_TEXTURE2D_DESC td{};td.Width=width;td.Height=height;td.MipLevels=td.ArraySize=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_IMMUTABLE;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{data.data(),width*4,0};Com<ID3D11Texture2D> tex;hrCheck(device->CreateTexture2D(&td,&initial,&tex),"PNG GPU");
        hrCheck(device->CreateShaderResourceView(tex.Get(),nullptr,&textures[name]),"PNG view");
    }
}
void Renderer::update(const Parameters& p){context->UpdateSubresource(constants.Get(),0,nullptr,&p,0,0);auto c=constants.Get();context->VSSetConstantBuffers(0,1,&c);context->PSSetConstantBuffers(0,1,&c);}
static LRESULT CALLBACK overlayProc(HWND w,UINT m,WPARAM a,LPARAM b){
    if(m==WM_NCHITTEST)return HTTRANSPARENT;if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;return DefWindowProc(w,m,a,b);
}
std::unique_ptr<Renderer::Overlay> Renderer::createOverlay(HINSTANCE instance,HMONITOR monitor) {
    WNDCLASS wc{};wc.hInstance=instance;wc.lpfnWndProc=overlayProc;wc.lpszClassName=L"FireflyMaimaiOverlay";RegisterClass(&wc);
    MONITORINFO mi{sizeof(mi)};if(!GetMonitorInfo(monitor,&mi))throw std::runtime_error("Monitor unavailable");
    auto o=std::make_unique<Overlay>();o->monitor=monitor;o->width=mi.rcMonitor.right-mi.rcMonitor.left;o->height=mi.rcMonitor.bottom-mi.rcMonitor.top;
    o->hwnd=CreateWindowEx(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT|WS_EX_NOREDIRECTIONBITMAP,wc.lpszClassName,L"",WS_POPUP,
        mi.rcMonitor.left,mi.rcMonitor.top,o->width,o->height,nullptr,nullptr,instance,nullptr);
    if(!o->hwnd)throw std::runtime_error("Overlay window failed");
    Com<IDXGIDevice> dx;Com<IDXGIAdapter> adapter;Com<IDXGIFactory2> factory;
    hrCheck(device.As(&dx),"DXGI device");hrCheck(dx->GetAdapter(&adapter),"Adapter");hrCheck(adapter->GetParent(IID_PPV_ARGS(&factory)),"Factory");
    DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=o->width;desc.Height=o->height;desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.Scaling=DXGI_SCALING_STRETCH;desc.AlphaMode=DXGI_ALPHA_MODE_PREMULTIPLIED;
    hrCheck(factory->CreateSwapChainForComposition(device.Get(),&desc,nullptr,&o->swap),"Composition swap chain");
    Com<IDXGISwapChain3> swap3;if(SUCCEEDED(o->swap.As(&swap3)))hrCheck(swap3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709),"scRGB color space");
    Com<ID3D11Texture2D> back;hrCheck(o->swap->GetBuffer(0,IID_PPV_ARGS(&back)),"Back buffer");hrCheck(device->CreateRenderTargetView(back.Get(),nullptr,&o->output),"Output target");
    D3D11_TEXTURE2D_DESC td{};td.Width=o->width;td.Height=o->height;td.ArraySize=td.MipLevels=1;td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    Com<ID3D11Texture2D> intermediate;hrCheck(device->CreateTexture2D(&td,nullptr,&intermediate),"Intermediate");
    hrCheck(device->CreateRenderTargetView(intermediate.Get(),nullptr,&o->intermediate),"Intermediate target");hrCheck(device->CreateShaderResourceView(intermediate.Get(),nullptr,&o->source),"Intermediate view");
    hrCheck(DCompositionCreateDevice(dx.Get(),IID_PPV_ARGS(&o->composition)),"DirectComposition");
    hrCheck(o->composition->CreateTargetForHwnd(o->hwnd,TRUE,&o->target),"Composition target");
    hrCheck(o->composition->CreateVisual(&o->visual),"Visual");hrCheck(o->visual->SetContent(o->swap.Get()),"Visual content");
    hrCheck(o->target->SetRoot(o->visual.Get()),"Visual root");hrCheck(o->composition->Commit(),"Composition commit");
    return o;
}
void Renderer::scene(ID3D11RenderTargetView* target,int width,int height,const Simulation& sim,double now){
    ID3D11ShaderResourceView* empty[2]{};context->PSSetShaderResources(0,2,empty);
    float clear[4]{};context->ClearRenderTargetView(target,clear);context->OMSetRenderTargets(1,&target,nullptr);
    D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};context->RSSetViewports(1,&viewport);context->RSSetState(raster.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context->VSSetShader(spriteVS.Get(),nullptr,0);
    auto samp=sampler.Get();context->PSSetSamplers(0,1,&samp);
    double scale=std::max(width,height)/1000.;
    // Same material ordering as the macOS scene: color, rings, then stars/spikes.
    for(int layer=0;layer<3;++layer)for(auto& b:sim.bursts)for(auto& p:b.particles){
        auto& e=sim.emitters[p.emitter];int mat=e["material"];
        if((mat==49?0:mat==50?1:2)!=layer)continue;
        double age=now-b.start-p.birth;if(age<0 || age>p.life)continue;double t=age/p.life;
        double distance=p.radius;
        // Fixed subdivisions keep the speed-limit integral independent of refresh rate.
        int steps=std::max(1,int(std::ceil(age*120)));double dt=age/steps;
        for(int j=0;j<steps;++j){double limit=e["velocityLimit"].is_null()?p.speed:curve(e["velocityLimit"],(j+.5)*dt/p.life,p.random)*100;
            distance+=std::min(p.speed,std::max(0.,limit))*dt;}
        auto color=gradient(e["colorHi"],t),low=gradient(e["colorLo"],t);
        if(e["colorRandom"].get<bool>())for(int c=0;c<4;++c)color[c]=float(lerp(low[c],color[c],p.random));
        Parameters v{};v.geometry[0]=float(width*.5+std::cos(p.angle)*distance*scale);
        v.geometry[1]=float(height*.5-std::sin(p.angle)*distance*scale);
        v.geometry[2]=float(p.diameter*std::max(0.,curve(e["sizeCurve"],t,p.random))*scale);v.geometry[3]=float(-p.rotation-p.spin*age);
        std::copy(color.begin(),color.end(),v.tint);v.frame[0]=float(width);v.frame[1]=float(height);v.frame[2]=float(std::fmod(now,1000.));v.frame[3]=e["exposure"].get<float>();v.options[0]=float(mat);update(v);
        ID3D11ShaderResourceView* views[2]{};for(size_t i=0;i<e["textures"].size();++i)views[i]=textures.at(e["textures"][i].get<std::string>()).Get();
        context->PSSetShaderResources(0,2,views);context->PSSetShader(mat==49?rainbowPS.Get():spritePS.Get(),nullptr,0);
        context->OMSetBlendState(mat==49?alphaBlend.Get():mat==50?addBlend.Get():screenBlend.Get(),nullptr,~0u);context->Draw(6,0);
    }
    context->OMSetRenderTargets(0,nullptr,nullptr);
}
void Renderer::finish(ID3D11RenderTargetView* target,ID3D11ShaderResourceView* source,int w,int h,float range,float white){
    Parameters v{};v.frame[0]=float(w);v.frame[1]=float(h);v.options[1]=range;v.options[2]=white;update(v);
    context->OMSetRenderTargets(1,&target,nullptr);context->OMSetBlendState(nullptr,nullptr,~0u);
    context->VSSetShader(fullVS.Get(),nullptr,0);context->PSSetShader(finishPS.Get(),nullptr,0);context->PSSetShaderResources(0,1,&source);context->Draw(3,0);
    ID3D11ShaderResourceView* empty=nullptr;context->PSSetShaderResources(0,1,&empty);context->OMSetRenderTargets(0,nullptr,nullptr);
}
static std::pair<float,float> displayRange(ID3D11Device* device,HMONITOR monitor,bool hdr) {
    float white=1,range=1;DXGI_OUTPUT_DESC1 desc{};bool found=false;
    Com<IDXGIDevice> dx;Com<IDXGIAdapter> adapter;Com<IDXGIFactory1> factory;
    if(FAILED(device->QueryInterface(IID_PPV_ARGS(&dx))) || FAILED(dx->GetAdapter(&adapter)) || FAILED(adapter->GetParent(IID_PPV_ARGS(&factory))))return {1,1};
    for(UINT a=0;!found;++a){Com<IDXGIAdapter1> gpu;if(factory->EnumAdapters1(a,&gpu)==DXGI_ERROR_NOT_FOUND)break;
        for(UINT i=0;;++i){Com<IDXGIOutput> output;Com<IDXGIOutput6> advanced;
            if(gpu->EnumOutputs(i,&output)==DXGI_ERROR_NOT_FOUND)break;
            if(SUCCEEDED(output.As(&advanced))&&SUCCEEDED(advanced->GetDesc1(&desc))&&desc.Monitor==monitor){found=true;break;}}}
    if(!found||desc.ColorSpace!=DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)return {1,1};
    MONITORINFOEX mi{};mi.cbSize=sizeof(mi);GetMonitorInfo(monitor,&mi);
    UINT paths=0,modes=0;
    if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&paths,&modes)==ERROR_SUCCESS){
        std::vector<DISPLAYCONFIG_PATH_INFO> ps(paths);std::vector<DISPLAYCONFIG_MODE_INFO> ms(modes);
        if(QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&paths,ps.data(),&modes,ms.data(),nullptr)==ERROR_SUCCESS)for(UINT i=0;i<paths;++i){
            DISPLAYCONFIG_SOURCE_DEVICE_NAME name{};name.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof(name),ps[i].sourceInfo.adapterId,ps[i].sourceInfo.id};
            if(DisplayConfigGetDeviceInfo(&name.header)==ERROR_SUCCESS && wcscmp(name.viewGdiDeviceName,mi.szDevice)==0){
                DISPLAYCONFIG_SDR_WHITE_LEVEL level{};level.header={DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL,sizeof(level),ps[i].targetInfo.adapterId,ps[i].targetInfo.id};
                if(DisplayConfigGetDeviceInfo(&level.header)==ERROR_SUCCESS)white=std::max(1.f,level.SDRWhiteLevel/1000.f);
            }
        }
    }
    if(hdr && std::isfinite(desc.MaxLuminance))range=std::clamp(desc.MaxLuminance/(80*white),1.f,16.f);
    return {range,white};
}
void Renderer::draw(Overlay& o,const Simulation& sim,double now,bool hdr){
    auto [range,white]=displayRange(device.Get(),o.monitor,hdr);
    scene(o.intermediate.Get(),o.width,o.height,sim,now);finish(o.output.Get(),o.source.Get(),o.width,o.height,range,white);
    hrCheck(o.swap->Present(0,0),"Present");ShowWindow(o.hwnd,SW_SHOWNOACTIVATE);
}
FrameProof Renderer::proof(const Simulation& sim,double now,float headroom,const fs::path& png){
    const UINT w=960,h=600;D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=1;d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    Com<ID3D11Texture2D> intermediate,out,staging;Com<ID3D11RenderTargetView> ir,orr;Com<ID3D11ShaderResourceView> srv;
    hrCheck(device->CreateTexture2D(&d,nullptr,&intermediate),"Proof input");hrCheck(device->CreateRenderTargetView(intermediate.Get(),nullptr,&ir),"Proof input view");hrCheck(device->CreateShaderResourceView(intermediate.Get(),nullptr,&srv),"Proof SRV");
    d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;d.BindFlags=D3D11_BIND_RENDER_TARGET;hrCheck(device->CreateTexture2D(&d,nullptr,&out),"Proof output");hrCheck(device->CreateRenderTargetView(out.Get(),nullptr,&orr),"Proof output view");
    scene(ir.Get(),w,h,sim,now);finish(orr.Get(),srv.Get(),w,h,headroom,1);
    d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;hrCheck(device->CreateTexture2D(&d,nullptr,&staging),"Proof readback");context->CopyResource(staging.Get(),out.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};hrCheck(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Proof map");
    FrameProof result;std::vector<uint8_t> pixels(size_t(w)*h*4);
    for(UINT y=0;y<h;++y){auto row=reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(mapped.pData)+y*mapped.RowPitch);
        for(UINT x=0;x<w;++x){float a=DirectX::PackedVector::XMConvertHalfToFloat(row[x*4+3]);if(x==0&&y==0)result.cornerAlpha=a;if(a>.05)++result.visible;
            for(int c=0;c<3;++c){float v=DirectX::PackedVector::XMConvertHalfToFloat(row[x*4+c]);if(std::isfinite(v))result.peak=std::max(result.peak,v);
                float linear=std::clamp(a>0?v/a:0.f,0.f,1.f);float srgb=linear<=.0031308f?linear*12.92f:1.055f*std::pow(linear,1/2.4f)-.055f;
                pixels[(size_t(y)*w+x)*4+(2-c)]=uint8_t(std::clamp(srgb*255,0.f,255.f));}
            pixels[(size_t(y)*w+x)*4+3]=uint8_t(std::clamp(a*255,0.f,255.f));}}
    context->Unmap(staging.Get(),0);
    if(!png.empty()){
        Com<IWICImagingFactory> wic;Com<IWICStream> stream;Com<IWICBitmapEncoder> encoder;Com<IWICBitmapFrameEncode> frame;
        hrCheck(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)),"Proof WIC");hrCheck(wic->CreateStream(&stream),"Proof stream");hrCheck(stream->InitializeFromFilename(png.c_str(),GENERIC_WRITE),"Proof PNG");
        hrCheck(wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder),"PNG encoder");hrCheck(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"PNG init");hrCheck(encoder->CreateNewFrame(&frame,nullptr),"PNG frame");hrCheck(frame->Initialize(nullptr),"Frame init");hrCheck(frame->SetSize(w,h),"PNG size");
        WICPixelFormatGUID fmt=GUID_WICPixelFormat32bppBGRA;hrCheck(frame->SetPixelFormat(&fmt),"PNG format");hrCheck(frame->WritePixels(h,w*4,UINT(pixels.size()),pixels.data()),"PNG write");hrCheck(frame->Commit(),"PNG frame commit");hrCheck(encoder->Commit(),"PNG commit");
    }
    return result;
}
