#include "audio.h"
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wtsapi32.h>
#include <chrono>
#include <iostream>
static constexpr UINT TRAY=WM_APP+1;
static constexpr int ENABLE=101,FIREWORK=102,HDR=103,ALL=104,PACK=105,VOLUME=106,PREVIEW=107,IMPORT=108,QUIT=109;
static double clockSeconds(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static std::wstring wide(const std::string& s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring r(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),r.data(),n);return r;}
static std::wstring text(HWND w){int n=GetWindowTextLength(w);std::wstring result(n+1,0);GetWindowText(w,result.data(),n+1);result.resize(n);return result;}
class Application {
    HINSTANCE instance;HWND window=nullptr;NOTIFYICONDATA tray{};
    fs::path exe,config,assetFolder;Settings settings;bool locked=false,ready=false;
    std::unique_ptr<Audio> audio;std::unique_ptr<Renderer> renderer;Simulation sim;
    std::vector<std::unique_ptr<Renderer::Overlay>> overlays;
    std::array<bool,256> held{};HFONT font=nullptr;HWND status=nullptr;
    double idleCleanup=0;
    static LRESULT CALLBACK procedure(HWND w,UINT m,WPARAM a,LPARAM b){
        Application* app=reinterpret_cast<Application*>(GetWindowLongPtr(w,GWLP_USERDATA));
        if(m==WM_NCCREATE){app=static_cast<Application*>(reinterpret_cast<CREATESTRUCT*>(b)->lpCreateParams);app->window=w;SetWindowLongPtr(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
        if(app)try{return app->message(m,a,b);}catch(const std::exception& e){app->stopEffects();app->setStatus(L"错误："+wide(e.what()));}
        return DefWindowProc(w,m,a,b);
    }
    void setStatus(const std::wstring& s){if(status)SetWindowText(status,s.c_str());}
    void save(){fs::create_directories(config.parent_path());auto tmp=config;tmp+=".tmp";
        {std::ofstream f(tmp);f<<settings.json().dump(2);if(!f)throw std::runtime_error("Cannot save settings");}
        if(!MoveFileEx(tmp.c_str(),config.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace settings");
    }
    void stopEffects(){KillTimer(window,1);overlays.clear();sim.bursts.clear();}
    void loadAssets(const fs::path& path){
        Simulation candidate;candidate.load(path);
        auto r=std::make_unique<Renderer>(exe,path,candidate);
        auto a=std::make_unique<Audio>(path);
        stopEffects();if(audio)audio->stop();sim=std::move(candidate);renderer=std::move(r);audio=std::move(a);assetFolder=path;ready=true;
        setStatus(audio->available?L"已就绪 · 混合模式按 Tab 播放烟花":L"未找到音频设备 · 连接设备后点击预览重试");
    }
    void controls(){
        font=CreateFont(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        auto add=[&](const wchar_t* cls,const wchar_t* label,DWORD style,int x,int y,int w,int h,int id){HWND c=CreateWindowEx(0,cls,label,WS_CHILD|WS_VISIBLE|style,x,y,w,h,window,reinterpret_cast<HMENU>(INT_PTR(id)),instance,nullptr);SendMessage(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return c;};
        add(L"STATIC",L"Thock maimai",0,24,20,410,28,0);
        add(L"STATIC",L"键盘音效与全屏烟花 · 单独运行即可",0,24,54,420,24,0);
        add(L"BUTTON",L"启用键盘音效",BS_AUTOCHECKBOX|WS_TABSTOP,24,94,225,26,ENABLE);
        add(L"BUTTON",L"启用烟花",BS_AUTOCHECKBOX|WS_TABSTOP,260,94,190,26,FIREWORK);
        add(L"BUTTON",L"HDR 高光（默认关闭）",BS_AUTOCHECKBOX|WS_TABSTOP,24,130,240,26,HDR);
        add(L"BUTTON",L"所有显示器",BS_AUTOCHECKBOX|WS_TABSTOP,280,130,170,26,ALL);
        add(L"STATIC",L"音效模式",0,24,176,105,24,0);
        auto combo=add(WC_COMBOBOX,L"",CBS_DROPDOWNLIST|WS_TABSTOP,130,170,310,250,PACK);
        for(auto label:{L"混合 · Tab 烟花",L"正解音",L"Tap",L"Touch",L"绝赞",L"烟花 Touch"})SendMessage(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        add(L"STATIC",L"音量",0,24,222,100,24,0);auto slider=add(TRACKBAR_CLASS,L"",TBS_HORZ|WS_TABSTOP,124,216,320,36,VOLUME);
        SendMessage(slider,TBM_SETRANGE,TRUE,MAKELPARAM(0,100));SendMessage(slider,TBM_SETPOS,TRUE,int(settings.volume*100));
        add(L"BUTTON",L"预览烟花与音效",BS_PUSHBUTTON|WS_TABSTOP,24,270,205,38,PREVIEW);
        add(L"BUTTON",L"选择素材文件夹…",BS_PUSHBUTTON|WS_TABSTOP,245,270,205,38,IMPORT);
        add(L"STATIC",L"混合：普通键 Tap，空格 Touch，回车绝赞，\n退格 / Esc / Delete 正解音，Tab 烟花。\n关闭窗口后留在托盘；右键托盘可退出。",0,24,330,440,72,0);
        status=add(L"STATIC",L"加载中…",0,24,414,440,55,0);
        for(auto [id,value]:{std::pair{ENABLE,settings.enabled},{FIREWORK,settings.fireworks},{HDR,settings.hdr},{ALL,settings.allDisplays}})CheckDlgButton(window,id,value?BST_CHECKED:BST_UNCHECKED);
        SendMessage(combo,CB_SETCURSEL,settings.pack,0);
    }
    void selectAssets(){
        Com<IFileOpenDialog> dialog;hrCheck(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)),"Folder chooser");
        DWORD flags=0;dialog->GetOptions(&flags);dialog->SetOptions(flags|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST);
        dialog->SetTitle(L"选择包含 maimai-fireworks.json 和 WAV 的素材文件夹");
        if(dialog->Show(window)!=S_OK)return;Com<IShellItem> item;hrCheck(dialog->GetResult(&item),"Folder");PWSTR path=nullptr;
        hrCheck(item->GetDisplayName(SIGDN_FILESYSPATH,&path),"Folder path");fs::path folder(path);CoTaskMemFree(path);
        loadAssets(folder);settings.assetPath=folder.u8string();save();
    }
    void trigger(Sound sound,bool preview=false){
        if(!ready||locked||(!preview&&!settings.enabled))return;
        if(preview&&!audio->available)audio->initialize();
        bool accepted=audio->play(sound,settings.volume);
        if(!accepted){if(preview)setStatus(settings.volume<=0?L"音量为 0，请调高音量后预览":L"音频设备不可用，请检查系统输出设备");return;}
        idleCleanup=clockSeconds()+5;SetTimer(window,2,1000,nullptr);
        if(sound!=Sound::Firework||!settings.fireworks)return;
        std::vector<HMONITOR> monitors;
        if(settings.allDisplays)EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR h,HDC,LPRECT,LPARAM p)->BOOL{reinterpret_cast<std::vector<HMONITOR>*>(p)->push_back(h);return TRUE;},reinterpret_cast<LPARAM>(&monitors));
        else {POINT p{};GetCursorPos(&p);monitors.push_back(MonitorFromPoint(p,MONITOR_DEFAULTTONEAREST));}
        bool match=overlays.size()==monitors.size();for(size_t i=0;match&&i<monitors.size();++i)match=overlays[i]->monitor==monitors[i];
        if(!match){stopEffects();for(auto h:monitors)overlays.push_back(renderer->createOverlay(instance,h));}
        sim.burst(clockSeconds());SetTimer(window,1,16,nullptr);
    }
    void trayMenu(){HMENU menu=CreatePopupMenu();AppendMenu(menu,MF_STRING,1,L"打开设置");AppendMenu(menu,MF_STRING|(settings.enabled?MF_CHECKED:0),2,L"启用");AppendMenu(menu,MF_SEPARATOR,0,nullptr);AppendMenu(menu,MF_STRING,3,L"退出");
        POINT p{};GetCursorPos(&p);SetForegroundWindow(window);int id=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,p.x,p.y,0,window,nullptr);DestroyMenu(menu);
        if(id==1){ShowWindow(window,SW_SHOW);SetForegroundWindow(window);}if(id==2){settings.enabled=!settings.enabled;CheckDlgButton(window,ENABLE,settings.enabled?BST_CHECKED:BST_UNCHECKED);if(!settings.enabled){stopEffects();if(audio)audio->stop();}save();}if(id==3)DestroyWindow(window);
    }
    LRESULT message(UINT m,WPARAM a,LPARAM b){
        switch(m){
        case WM_CLOSE:ShowWindow(window,SW_HIDE);return 0;
        case WM_DESTROY:stopEffects();if(audio)audio->stop();WTSUnRegisterSessionNotification(window);Shell_NotifyIcon(NIM_DELETE,&tray);PostQuitMessage(0);return 0;
        case WM_INPUT:{UINT size=0;GetRawInputData(reinterpret_cast<HRAWINPUT>(b),RID_INPUT,nullptr,&size,sizeof(RAWINPUTHEADER));
            if(size>4096||!size)break;std::vector<uint8_t> bytes(size);
            if(GetRawInputData(reinterpret_cast<HRAWINPUT>(b),RID_INPUT,bytes.data(),&size,sizeof(RAWINPUTHEADER))!=size)break;
            auto* input=reinterpret_cast<RAWINPUT*>(bytes.data());if(input->header.dwType!=RIM_TYPEKEYBOARD)break;
            unsigned key=input->data.keyboard.VKey;if(key>=256)break;bool up=(input->data.keyboard.Flags&RI_KEY_BREAK)!=0;
            if(up){held[key]=false;break;}if(held[key])break;held[key]=true;
            if(key==VK_SHIFT||key==VK_CONTROL||key==VK_MENU||key==VK_LWIN||key==VK_RWIN)break;
            // Do not echo keyboard navigation in this app's own settings window.
            if(GetForegroundWindow()!=window)trigger(route(key,settings.pack));break;}
        case WM_COMMAND:{int id=LOWORD(a);
            if(id==ENABLE||id==FIREWORK||id==HDR||id==ALL){settings.enabled=IsDlgButtonChecked(window,ENABLE)==BST_CHECKED;settings.fireworks=IsDlgButtonChecked(window,FIREWORK)==BST_CHECKED;
                settings.hdr=IsDlgButtonChecked(window,HDR)==BST_CHECKED;settings.allDisplays=IsDlgButtonChecked(window,ALL)==BST_CHECKED;stopEffects();if(!settings.enabled&&audio)audio->stop();save();}
            if(id==PACK&&HIWORD(a)==CBN_SELCHANGE){settings.pack=int(SendDlgItemMessage(window,PACK,CB_GETCURSEL,0,0));stopEffects();if(audio)audio->stop();save();}
            if(id==PREVIEW){trigger(Sound::Firework,true);if(!settings.fireworks)setStatus(L"烟花开关已关闭，当前只播放音效");}
            if(id==IMPORT)selectAssets();return 0;}
        case WM_HSCROLL:settings.volume=float(SendDlgItemMessage(window,VOLUME,TBM_GETPOS,0,0))/100;save();if(settings.volume==0){stopEffects();if(audio)audio->stop();}return 0;
        case WM_TIMER:if(a==1){double now=clockSeconds();sim.expire(now);if(sim.bursts.empty()){stopEffects();return 0;}for(auto& o:overlays)renderer->draw(*o,sim,now,settings.hdr);}
            if(a==2&&audio){audio->reap();if(clockSeconds()>idleCleanup)KillTimer(window,2);}return 0;
        case WM_DISPLAYCHANGE:stopEffects();held.fill(false);return 0;
        case WM_DPICHANGED:{auto r=reinterpret_cast<RECT*>(b);SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOACTIVATE|SWP_NOZORDER);return 0;}
        case WM_POWERBROADCAST:if(a==PBT_APMSUSPEND){stopEffects();held.fill(false);if(audio)audio->stop();}if(a==PBT_APMRESUMEAUTOMATIC&&audio){held.fill(false);audio->initialize();}return TRUE;
        case WM_WTSSESSION_CHANGE:if(a==WTS_SESSION_LOCK){locked=true;held.fill(false);stopEffects();if(audio)audio->stop();}if(a==WTS_SESSION_UNLOCK){locked=false;held.fill(false);}return 0;
        case WM_DEVICECHANGE:held.fill(false);return 0;
        case TRAY:if(LOWORD(b)==WM_RBUTTONUP)trayMenu();if(LOWORD(b)==WM_LBUTTONUP){ShowWindow(window,SW_SHOW);SetForegroundWindow(window);}return 0;
        }
        static const UINT taskbarCreated=RegisterWindowMessage(L"TaskbarCreated");if(m==taskbarCreated&&tray.hWnd)Shell_NotifyIcon(NIM_ADD,&tray);
        return DefWindowProc(window,m,a,b);
    }
public:
    Application(HINSTANCE h,const fs::path& root):instance(h),exe(root){
        PWSTR appdata=nullptr;hrCheck(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&appdata),"AppData");config=fs::path(appdata)/"ThockMaimai"/"settings.json";CoTaskMemFree(appdata);
        if(fs::exists(config))try{settings=Settings::parse(Json::parse(readText(config)));}catch(...){settings=Settings{};}
        WNDCLASS wc{};wc.hInstance=h;wc.lpfnWndProc=procedure;wc.lpszClassName=L"FireflyMaimaiSettings";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);wc.hIcon=LoadIcon(nullptr,IDI_APPLICATION);RegisterClass(&wc);
        HWND w=CreateWindow(wc.lpszClassName,L"Thock maimai",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,500,530,nullptr,nullptr,h,this);
        if(!w)throw std::runtime_error("Settings window failed");controls();
        tray.cbSize=sizeof(tray);tray.hWnd=window;tray.uID=1;tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;tray.uCallbackMessage=TRAY;tray.hIcon=wc.hIcon;wcscpy_s(tray.szTip,L"Thock maimai");Shell_NotifyIcon(NIM_ADD,&tray);
        RAWINPUTDEVICE keyboard{1,6,RIDEV_INPUTSINK|RIDEV_DEVNOTIFY,window};if(!RegisterRawInputDevices(&keyboard,1,sizeof(keyboard)))throw std::runtime_error("Cannot register keyboard input");
        WTSRegisterSessionNotification(window,NOTIFY_FOR_THIS_SESSION);
        try{loadAssets(settings.assetPath.empty()?exe/"assets":fs::u8path(settings.assetPath));}catch(const std::exception& e){setStatus(L"请选择有效素材文件夹："+wide(e.what()));}
        ShowWindow(window,SW_SHOW);UpdateWindow(window);
    }
    ~Application(){if(font)DeleteObject(font);}
    int run(){MSG message{};while(GetMessage(&message,nullptr,0,0)>0){if(!IsDialogMessage(window,&message)){TranslateMessage(&message);DispatchMessage(&message);}}return int(message.wParam);}
};
static int selfTest(HINSTANCE instance,const fs::path& root,const fs::path& folder){
    Simulation sim;sim.load(folder);sim.burst(0);Renderer renderer(root,folder,sim,true);
    auto sdr=renderer.proof(sim,.20,1,root/"preview-sdr.png"),hdr=renderer.proof(sim,.20,16,{});
    Json proof={{"sdrPeak",sdr.peak},{"hdrPeak",hdr.peak},{"transparentCorner",sdr.cornerAlpha},{"visiblePixels",sdr.visible},{"defaultHdr",Settings{}.hdr}};
    if(sdr.peak>1.001||hdr.peak<1.1||sdr.cornerAlpha>.001||sdr.visible<100)throw std::runtime_error("Pixel validation failed: "+proof.dump());
    Audio audio(folder);for(auto& wave:audio.waves)if(wave.pcm.empty())throw std::runtime_error("Empty audio");proof["wavFilesValidated"]=audio.waves.size();proof["audioDeviceAvailable"]=audio.available;
    auto o=renderer.createOverlay(instance,MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY));renderer.draw(*o,sim,.2,false);
    proof["overlayCreated"]=IsWindowVisible(o->hwnd)!=FALSE;proof["noActivate"]=(GetWindowLongPtr(o->hwnd,GWL_EXSTYLE)&WS_EX_NOACTIVATE)!=0;
    proof["clickThrough"]=(GetWindowLongPtr(o->hwnd,GWL_EXSTYLE)&WS_EX_TRANSPARENT)!=0;
    o.reset();sim.expire(3);proof["burstsExpired"]=sim.bursts.empty();
    std::ofstream(root/"self-test.json")<<proof.dump(2);return 0;
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int){
    HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(com))return 2;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_BAR_CLASSES};InitCommonControlsEx(&controls);
    wchar_t exePath[32768]{};GetModuleFileName(nullptr,exePath,32768);fs::path root=fs::path(exePath).parent_path();
    int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);bool test=argc>1&&std::wstring(argv[1])==L"--self-test";
    fs::path assets=argc>2?fs::path(argv[2]):root/"assets";LocalFree(argv);
    int result=0;HANDLE mutex=nullptr;
    try{
        if(test)result=selfTest(instance,root,assets);
        else {mutex=CreateMutex(nullptr,FALSE,L"Local\\FireflyThockMaimai");if(GetLastError()==ERROR_ALREADY_EXISTS){HWND old=FindWindow(L"FireflyMaimaiSettings",nullptr);if(old){ShowWindow(old,SW_SHOW);SetForegroundWindow(old);}}
            else {Application app(instance,root);result=app.run();}}
    }catch(const std::exception& e){std::ofstream(root/"error.txt")<<e.what();if(!test)MessageBox(nullptr,wide(e.what()).c_str(),L"Thock maimai",MB_OK|MB_ICONERROR);result=1;}
    if(mutex)CloseHandle(mutex);CoUninitialize();return result;
}
