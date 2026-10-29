#include "audio.h"
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wtsapi32.h>
#include <chrono>
#include <iostream>
static constexpr UINT TRAY=WM_APP+1;
static constexpr int ENABLE=101,FIREWORK=102,HDR=103,ALL=104,PACK=105,VOLUME=106,PREVIEW=107,IMPORT=108,QUIT=109,RESET=110,LICENSES=111;
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
    bool testing=false;
    size_t inputEvents=0,fireworkCount=0,drawnFrames=0;
    std::array<size_t,6> soundRequests{};
    std::string lastFailure;
    std::vector<std::pair<HWND,RECT>> controlLayout;
    static LRESULT CALLBACK procedure(HWND w,UINT m,WPARAM a,LPARAM b){
        Application* app=reinterpret_cast<Application*>(GetWindowLongPtr(w,GWLP_USERDATA));
        if(m==WM_NCCREATE){app=static_cast<Application*>(reinterpret_cast<CREATESTRUCT*>(b)->lpCreateParams);app->window=w;SetWindowLongPtr(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
        if(app)try{return app->message(m,a,b);}catch(const std::exception& e){app->lastFailure=e.what();app->stopEffects();app->setStatus(L"错误："+wide(e.what()));}
        return DefWindowProc(w,m,a,b);
    }
    void setStatus(const std::wstring& s){if(status)SetWindowText(status,s.c_str());}
    void save(){fs::create_directories(config.parent_path());auto tmp=config;tmp+=".tmp";
        {std::ofstream f(tmp);f<<settings.json().dump(2);if(!f)throw std::runtime_error("Cannot save settings");}
        if(!MoveFileEx(tmp.c_str(),config.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace settings");
    }
    void stopEffects(){KillTimer(window,1);overlays.clear();sim.bursts.clear();}
    void loadAssets(const fs::path& path){
        Assets assets(path); Simulation candidate;candidate.loadJson(Json::parse(assets.read("maimai-fireworks.json")));
        auto r=std::make_unique<Renderer>(assets,candidate);
        auto a=std::make_unique<Audio>(assets);
        stopEffects();if(audio)audio->stop();sim=std::move(candidate);renderer=std::move(r);audio=std::move(a);assetFolder=path;ready=true;
        setStatus(audio->available?L"已就绪 · 混合模式按 Tab 播放烟花":L"未找到音频设备 · 烟花仍可使用，连接设备后点击预览重试");
    }
    void controls(){
        auto add=[&](const wchar_t* cls,const wchar_t* label,DWORD style,int x,int y,int w,int h,int id){HWND c=CreateWindowEx(0,cls,label,WS_CHILD|WS_VISIBLE|style,x,y,w,h,window,reinterpret_cast<HMENU>(INT_PTR(id)),instance,nullptr);controlLayout.push_back({c,{x,y,x+w,y+h}});return c;};
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
        add(L"BUTTON",L"恢复内置素材",BS_PUSHBUTTON|WS_TABSTOP,24,322,205,32,RESET);
        add(L"BUTTON",L"许可与开源",BS_PUSHBUTTON|WS_TABSTOP,245,322,205,32,LICENSES);
        add(L"STATIC",L"混合：普通键 Tap，空格 Touch，回车绝赞，\n退格 / Esc / Delete 正解音，Tab 烟花。\n关闭窗口后留在托盘；右键托盘可退出。",0,24,372,440,72,0);
        status=add(L"STATIC",L"加载中…",0,24,454,440,55,0);
        for(auto [id,value]:{std::pair{ENABLE,settings.enabled},{FIREWORK,settings.fireworks},{HDR,settings.hdr},{ALL,settings.allDisplays}})CheckDlgButton(window,id,value?BST_CHECKED:BST_UNCHECKED);
        SendMessage(combo,CB_SETCURSEL,settings.pack,0);
    }
    void scaleControls(UINT dpi){
        HFONT previous=font;
        font=CreateFont(-MulDiv(16,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        for(auto& [child,r]:controlLayout){SetWindowPos(child,nullptr,MulDiv(r.left,dpi,96),MulDiv(r.top,dpi,96),MulDiv(r.right-r.left,dpi,96),MulDiv(r.bottom-r.top,dpi,96),SWP_NOZORDER|SWP_NOACTIVATE);SendMessage(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);}
        if(previous)DeleteObject(previous);
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
        if(settings.volume<=0){if(preview)setStatus(L"音量为 0，请调高音量后预览");return;}
        if(testing)++soundRequests[size_t(sound)];
        if(preview&&!audio->available)audio->initialize();
        bool accepted=audio->play(sound,settings.volume);
        if(!accepted)setStatus(L"音频设备不可用 · 烟花继续播放，请检查系统输出设备");
        else {idleCleanup=clockSeconds()+5;SetTimer(window,2,1000,nullptr);}
        if(sound!=Sound::Firework||!settings.fireworks)return;
        std::vector<HMONITOR> monitors;
        if(settings.allDisplays)EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR h,HDC,LPRECT,LPARAM p)->BOOL{reinterpret_cast<std::vector<HMONITOR>*>(p)->push_back(h);return TRUE;},reinterpret_cast<LPARAM>(&monitors));
        else {POINT p{};GetCursorPos(&p);monitors.push_back(MonitorFromPoint(p,MONITOR_DEFAULTTONEAREST));}
        bool match=overlays.size()==monitors.size();for(size_t i=0;match&&i<monitors.size();++i)match=overlays[i]->monitor==monitors[i];
        if(!match){stopEffects();for(auto h:monitors)overlays.push_back(renderer->createOverlay(instance,h));}
        sim.burst(clockSeconds());++fireworkCount;SetTimer(window,1,16,nullptr);
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
            if(testing)++inputEvents;
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
            if(id==IMPORT)selectAssets();
            if(id==RESET){loadAssets({});settings.assetPath.clear();save();}
            if(id==LICENSES){auto licenses=Assets::embedded("LICENSE.txt")+"\n\n"+Assets::embedded("json.LICENSE.txt")+"\n\n"+Assets::embedded("ASSET-LICENSE.txt");MessageBox(window,wide(licenses).c_str(),L"Thock maimai · 许可",MB_OK);}
            return 0;}
        case WM_HSCROLL:settings.volume=float(SendDlgItemMessage(window,VOLUME,TBM_GETPOS,0,0))/100;save();if(settings.volume==0){stopEffects();if(audio)audio->stop();}return 0;
        case WM_TIMER:if(a==1){double now=clockSeconds();sim.expire(now);if(sim.bursts.empty()){stopEffects();return 0;}for(auto& o:overlays)renderer->draw(*o,sim,now,settings.hdr);++drawnFrames;}
            if(a==2&&audio){audio->reap();if(clockSeconds()>idleCleanup)KillTimer(window,2);}return 0;
        case WM_DISPLAYCHANGE:stopEffects();held.fill(false);return 0;
        case WM_DPICHANGED:{auto r=reinterpret_cast<RECT*>(b);SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOACTIVATE|SWP_NOZORDER);if(!controlLayout.empty())scaleControls(HIWORD(a));return 0;}
        case WM_POWERBROADCAST:if(a==PBT_APMSUSPEND){stopEffects();held.fill(false);if(audio)audio->stop();}if(a==PBT_APMRESUMEAUTOMATIC&&audio){held.fill(false);audio->initialize();}return TRUE;
        case WM_WTSSESSION_CHANGE:if(a==WTS_SESSION_LOCK){locked=true;held.fill(false);stopEffects();if(audio)audio->stop();}if(a==WTS_SESSION_UNLOCK){locked=false;held.fill(false);}return 0;
        case WM_INPUT_DEVICE_CHANGE:held.fill(false);return 0;
        case TRAY:if(LOWORD(b)==WM_RBUTTONUP)trayMenu();if(LOWORD(b)==WM_LBUTTONUP){ShowWindow(window,SW_SHOW);SetForegroundWindow(window);}return 0;
        }
        static const UINT taskbarCreated=RegisterWindowMessage(L"TaskbarCreated");if(m==taskbarCreated&&tray.hWnd)Shell_NotifyIcon(NIM_ADD,&tray);
        return DefWindowProc(window,m,a,b);
    }
public:
    Application(HINSTANCE h,const fs::path& root,bool testMode=false):instance(h),exe(root),testing(testMode){
        PWSTR appdata=nullptr;hrCheck(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&appdata),"AppData");config=fs::path(appdata)/"ThockMaimai"/"settings.json";CoTaskMemFree(appdata);
        if(testing)config=root/"test-state"/"settings.json";
        else if(fs::exists(config))try{settings=Settings::parse(Json::parse(readText(config)));}catch(...){settings=Settings{};}
        WNDCLASS wc{};wc.hInstance=h;wc.lpfnWndProc=procedure;wc.lpszClassName=L"FireflyMaimaiSettings";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);wc.hIcon=LoadIcon(nullptr,IDI_APPLICATION);RegisterClass(&wc);
        HWND w=CreateWindow(wc.lpszClassName,L"Thock maimai",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,500,530,nullptr,nullptr,h,this);
        if(!w)throw std::runtime_error("Settings window failed");controls();UINT dpi=GetDpiForWindow(window);scaleControls(dpi);
        RECT desired{0,0,MulDiv(480,dpi,96),MulDiv(530,dpi,96)};
        AdjustWindowRectExForDpi(&desired,DWORD(GetWindowLongPtr(window,GWL_STYLE)),FALSE,0,dpi);
        SetWindowPos(window,nullptr,0,0,desired.right-desired.left,desired.bottom-desired.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        tray.cbSize=sizeof(tray);tray.hWnd=window;tray.uID=1;tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;tray.uCallbackMessage=TRAY;tray.hIcon=wc.hIcon;wcscpy_s(tray.szTip,L"Thock maimai");Shell_NotifyIcon(NIM_ADD,&tray);
        RAWINPUTDEVICE keyboard{1,6,RIDEV_INPUTSINK|RIDEV_DEVNOTIFY,window};if(!RegisterRawInputDevices(&keyboard,1,sizeof(keyboard)))throw std::runtime_error("Cannot register keyboard input");
        WTSRegisterSessionNotification(window,NOTIFY_FOR_THIS_SESSION);
        try{loadAssets(settings.assetPath.empty()?fs::path{}:fs::u8path(settings.assetPath));}catch(const std::exception& e){
            if(settings.assetPath.empty())throw;
            loadAssets({});settings.assetPath.clear();save();setStatus(L"原素材文件夹不可用，已恢复内置素材");
        }
        ShowWindow(window,SW_SHOW);UpdateWindow(window);
    }
    ~Application(){if(font)DeleteObject(font);}
    int run(){MSG message{};while(GetMessage(&message,nullptr,0,0)>0){if(!IsDialogMessage(window,&message)){TranslateMessage(&message);DispatchMessage(&message);}}return int(message.wParam);}
    void integrationCheck(const fs::path& output);
};
// Exercise Windows messages and SendInput against an app-owned foreground
// window. This path is opt-in, never records user keys, and keeps test settings
// beside the CI executable instead of modifying the user's preferences.
void Application::integrationCheck(const fs::path& output) {
    Json report={{"testEnvironment","Windows desktop integration"},{"embeddedAssets",true},
                 {"audioDeviceAvailable",audio && audio->available}};
    auto check=[&](const char* label,bool value){report[label]=value;
        if(!value){report["rawInputEvents"]=inputEvents;report["lastApplicationError"]=lastFailure;
            std::ofstream(output/"integration-test.json")<<report.dump(2);throw std::runtime_error(std::string("Integration check failed: ")+label);}};
    auto pump=[&](double seconds){double end=clockSeconds()+seconds;MSG m{};
        do {while(PeekMessage(&m,nullptr,0,0,PM_REMOVE)){if(m.message==WM_QUIT)break;
            if(!IsDialogMessage(window,&m)){TranslateMessage(&m);DispatchMessage(&m);}}
            Sleep(2);
        }while(clockSeconds()<end);
    };
    auto input=[&](WORD key,bool up){INPUT event{};event.type=INPUT_KEYBOARD;
        event.ki.wScan=WORD(MapVirtualKey(key,MAPVK_VK_TO_VSC));event.ki.dwFlags=KEYEVENTF_SCANCODE|(up?KEYEVENTF_KEYUP:0);
        if(SendInput(1,&event,sizeof(event))!=1)throw std::runtime_error("SendInput failed");pump(.12);};
    auto tap=[&](WORD key){input(key,false);input(key,true);};
    auto toggle=[&](int id,bool on){CheckDlgButton(window,id,on?BST_CHECKED:BST_UNCHECKED);SendMessage(window,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),0);};
    auto pack=[&](int index){SendDlgItemMessage(window,PACK,CB_SETCURSEL,index,0);SendMessage(window,WM_COMMAND,MAKEWPARAM(PACK,CBN_SELCHANGE),0);};
    auto volume=[&](int value){SendDlgItemMessage(window,VOLUME,TBM_SETPOS,TRUE,value);SendMessage(window,WM_HSCROLL,0,0);};
    check("resourcesLoaded",ready && sim.emitters.size()==6 && audio->waves.size()==6);
    check("defaultHDROff",IsDlgButtonChecked(window,HDR)==BST_UNCHECKED);
    check("sixPackChoices",SendDlgItemMessage(window,PACK,CB_GETCOUNT,0,0)==6);
    check("noExternalAssetPath",settings.assetPath.empty());
    WNDCLASS wc{};wc.hInstance=instance;wc.lpfnWndProc=DefWindowProc;wc.lpszClassName=L"ThockIntegrationTarget";
    wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClass(&wc);
    HWND target=CreateWindow(wc.lpszClassName,L"Thock CI keyboard target",WS_OVERLAPPEDWINDOW,40,40,450,240,nullptr,nullptr,instance,nullptr);
    ShowWindow(target,SW_SHOW);SetForegroundWindow(target);SetFocus(target);pump(.2);
    check("foregroundTarget",GetForegroundWindow()==target);
    tap('A');check("ordinaryKeyRoutesTap",soundRequests[size_t(Sound::Tap)]==1 && fireworkCount==0);
    tap(VK_TAB);pump(.15);
    check("tabTriggersFireworks",fireworkCount==1);
    check("overlayRendered",drawnFrames>0 && !overlays.empty() && IsWindowVisible(overlays.front()->hwnd));
    check("overlayDoesNotTakeFocus",GetForegroundWindow()==target);
    check("overlayHitTestTransparent",SendMessage(overlays.front()->hwnd,WM_NCHITTEST,0,0)==HTTRANSPARENT);
    size_t before=fireworkCount;
    input(VK_TAB,false);input(VK_TAB,false);input(VK_TAB,false);input(VK_TAB,true);
    check("heldKeyDeduplicated",fireworkCount==before+1);
    for(auto [key,sound]:{std::pair<WORD,Sound>{VK_SPACE,Sound::Touch},{VK_RETURN,Sound::BreakCritical},{VK_BACK,Sound::Answer},{VK_ESCAPE,Sound::Answer}}){
        size_t previous=soundRequests[size_t(sound)];tap(key);check(("route_"+std::to_string(key)).c_str(),soundRequests[size_t(sound)]==previous+1);
    }
    for(int index=1;index<=5;++index){pack(index);Sound sound=route('A',index);size_t previous=soundRequests[size_t(sound)];tap('A');
        check(("pack_"+std::to_string(index)).c_str(),soundRequests[size_t(sound)]==previous+1);}
    pack(0);toggle(FIREWORK,false);before=fireworkCount;size_t requests=soundRequests[size_t(Sound::Firework)];tap(VK_TAB);
    check("effectOffKeepsAudioRequest",fireworkCount==before && soundRequests[size_t(Sound::Firework)]==requests+1);
    toggle(FIREWORK,true);toggle(ENABLE,false);before=fireworkCount;requests=soundRequests[size_t(Sound::Firework)];tap(VK_TAB);
    check("masterSwitchStopsBoth",fireworkCount==before && soundRequests[size_t(Sound::Firework)]==requests);
    toggle(ENABLE,true);volume(0);before=fireworkCount;requests=soundRequests[size_t(Sound::Firework)];tap(VK_TAB);
    check("zeroVolumeStopsBoth",fireworkCount==before && soundRequests[size_t(Sound::Firework)]==requests);
    volume(35);tap(VK_TAB);pump(2.1);check("idleClearsAllOverlayWindows",overlays.empty() && sim.bursts.empty());
    before=fireworkCount;tap(VK_TAB);check("resumesAfterIdle",fireworkCount==before+1 && !overlays.empty());
    SendMessage(window,WM_DISPLAYCHANGE,0,0);check("displayChangeClearsOverlay",overlays.empty());
    before=fireworkCount;tap(VK_TAB);check("resumesAfterDisplayChange",fireworkCount==before+1);
    for(int i=0;i<12;++i){INPUT events[2]{};events[0].type=events[1].type=INPUT_KEYBOARD;
        events[0].ki.wScan=events[1].ki.wScan=WORD(MapVirtualKey(VK_TAB,MAPVK_VK_TO_VSC));
        events[0].ki.dwFlags=KEYEVENTF_SCANCODE;events[1].ki.dwFlags=KEYEVENTF_SCANCODE|KEYEVENTF_KEYUP;
        SendInput(2,events,sizeof(INPUT));pump(.025);}
    check("rapidBurstBound",sim.bursts.size()<=6);check("audioVoiceBound",audio->activeVoices()<=32);
    check("settingsRoundTrip",Settings::parse(Json::parse(readText(config))).volume==.35f);
    check("noApplicationError",lastFailure.empty());
    report["rawInputEvents"]=inputEvents;report["fireworksTriggered"]=fireworkCount;
    report["framesRendered"]=drawnFrames;report["soundRequests"]=soundRequests;
    report["audioBuffersSubmitted"]=audio->submitted;report["audioDeviceError"]=unsigned(audio->lastError);
    report["physicalAudioListeningVerified"]=false;
    if(audio->available)check("audioBuffersSubmittedToDevice",audio->submitted>0);
    std::ofstream(output/"integration-test.json")<<report.dump(2);
    DestroyWindow(target);DestroyWindow(window);
}

static int selfTest(HINSTANCE instance,const fs::path& root,const fs::path& folder){
    Assets assets(folder);Simulation sim;sim.loadJson(Json::parse(assets.read("maimai-fireworks.json")));sim.burst(0);Renderer renderer(assets,sim,true);
    auto sdr=renderer.proof(sim,.20,1,root/"preview-sdr.png"),hdr=renderer.proof(sim,.20,16,{});
    Json proof={{"sdrPeak",sdr.peak},{"hdrPeak",hdr.peak},{"transparentCorner",sdr.cornerAlpha},{"visiblePixels",sdr.visible},{"defaultHdr",Settings{}.hdr}};
    if(sdr.peak>1.001||hdr.peak<1.1||sdr.cornerAlpha>.001||sdr.visible<100)throw std::runtime_error("Pixel validation failed: "+proof.dump());
    Audio audio(assets);for(auto& wave:audio.waves)if(wave.pcm.empty())throw std::runtime_error("Empty audio");proof["wavFilesValidated"]=audio.waves.size();proof["audioDeviceAvailable"]=audio.available;
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
    bool integration=argc>1&&std::wstring(argv[1])==L"--integration-test";
    fs::path assets=argc>2?fs::path(argv[2]):fs::path{};LocalFree(argv);
    int result=0;HANDLE mutex=nullptr;
    try{
        if(test)result=selfTest(instance,root,assets);
        else if(integration){Application app(instance,root,true);app.integrationCheck(root);}
        else {mutex=CreateMutex(nullptr,FALSE,L"Local\\FireflyThockMaimai");if(GetLastError()==ERROR_ALREADY_EXISTS){HWND old=FindWindow(L"FireflyMaimaiSettings",nullptr);if(old){ShowWindow(old,SW_SHOW);SetForegroundWindow(old);}}
            else {Application app(instance,root);result=app.run();}}
    }catch(const std::exception& e){std::ofstream(root/"error.txt")<<e.what();if(!test&&!integration)MessageBox(nullptr,wide(e.what()).c_str(),L"Thock maimai",MB_OK|MB_ICONERROR);result=1;}
    if(mutex)CloseHandle(mutex);CoUninitialize();return result;
}
