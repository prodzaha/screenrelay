#include "network.hpp"
#include <shellapi.h>
#include <shlobj.h>
#include <sddl.h>
#include <taskschd.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <iomanip>
#include <iostream>
using namespace relay;
namespace fs=std::filesystem;
constexpr UINT WM_SAMPLE=WM_APP+1, WM_COMMAND_REMOTE=WM_APP+2;
constexpr int IP=101,MONITOR=102,TV=103,TEST=104,CAPTURE=105,SAVE=106,STARTUP=107,INFO=108,REFRESH=109,TRIAL_MONITOR=113,TRIAL_TV=114;
fs::path root;
HWND mainWindow=nullptr,settingsWindow=nullptr;
std::wstring className,mutexName;
std::mutex configMutex,sampleMutex;
std::condition_variable wakeWorker;
std::atomic<bool> quitWorker=false;
struct Config {
    std::string address,model,id;
    Profile monitor,tv,both;
    bool startup=false;
    bool monitorVerified=false,tvVerified=false;
    UINT monitorKey=VK_F11,tvKey=VK_F9,bothKey=VK_F10;
    unsigned generation=0;
    bool ready()const{return !address.empty() && !model.empty() && !id.empty() && monitor.size()==1 && tv.size()==1 && both.size()==2 && monitorVerified && tvVerified;}
    json save()const{return {{"version",1},{"address",address},{"model",model},{"id",id},{"monitor",saveProfile(monitor)},{"tv",saveProfile(tv)},{"both",saveProfile(both)},
        {"startup",startup},{"monitorVerified",monitorVerified},{"tvVerified",tvVerified},{"hotkeys",{{"monitor",monitorKey},{"tv",tvKey},{"both",bothKey}}}};}
    static Config load(const json& j){
        if(j.at("version")!=1)throw std::runtime_error("Unsupported settings version");
        Config c;c.address=j.at("address");c.model=j.at("model");c.id=j.at("id");
        if(!localAddress(c.address))throw std::runtime_error("TV address must be a private LAN IPv4 address");
        c.monitor=loadProfile(j.at("monitor"));c.tv=loadProfile(j.at("tv"));c.both=loadProfile(j.at("both"));c.startup=j.value("startup",false);
        c.monitorVerified=j.value("monitorVerified",false);c.tvVerified=j.value("tvVerified",false);
        if(j.contains("hotkeys")){c.monitorKey=j.at("hotkeys").at("monitor");c.tvKey=j.at("hotkeys").at("tv");c.bothKey=j.at("hotkeys").at("both");}
        for(UINT key:{c.monitorKey,c.tvKey,c.bothKey})if(key<VK_F1||key>VK_F24)throw std::runtime_error("Hotkeys must use Ctrl+Alt+F1 through F24");
        if(c.monitorKey==c.tvKey||c.monitorKey==c.bothKey||c.tvKey==c.bothKey)throw std::runtime_error("Hotkeys must be different");
        if(!consistentProfiles(c.monitor,c.tv,c.both))throw std::runtime_error("Invalid two-display setup");
        return c;
    }
} config;
SampleQueue pending;
std::atomic<bool> paused=true;
bool settingsPreviouslyPaused=true;
std::string lastError,lastSignal="unknown";
Controller controller;
std::vector<Identity> choices;
Config draft;
std::wstring executable(){std::wstring p(32768,0);DWORD n=GetModuleFileNameW(nullptr,p.data(),(DWORD)p.size());if(!n||n==p.size())throw std::runtime_error("Cannot locate executable");p.resize(n);return p;}
std::wstring userSid(){
    HANDLE token=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))throw std::runtime_error("Cannot read user identity");
    DWORD n=0;GetTokenInformation(token,TokenUser,nullptr,0,&n);std::vector<unsigned char>b(n);
    if(!GetTokenInformation(token,TokenUser,b.data(),n,&n)){CloseHandle(token);throw std::runtime_error("Cannot read user identity");}CloseHandle(token);
    LPWSTR text=nullptr;if(!ConvertSidToStringSidW(((TOKEN_USER*)b.data())->User.Sid,&text))throw std::runtime_error("Cannot read user SID");
    std::wstring result(text);LocalFree(text);return result;
}
json readJson(const fs::path& p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("Cannot read "+utf8(p.filename().wstring()));return json::parse(f);}
void writeJson(const fs::path& p,const json& j){
    auto temp=p;temp+=L".tmp";
    {std::ofstream f(temp,std::ios::binary|std::ios::trunc);if(!f)throw std::runtime_error("Cannot save local data");f<<j.dump(2);f.flush();if(!f)throw std::runtime_error("Cannot write local data");}
    if(!MoveFileExW(temp.c_str(),p.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot atomically replace local data");
}
void log(const std::string& message){
    try {
        auto p=root/L"screenrelay.log",old=root/L"screenrelay.previous.log";
        if(fs::exists(p)&&fs::file_size(p)>1024*1024){std::error_code ec;fs::remove(old,ec);fs::rename(p,old,ec);}
        SYSTEMTIME t{};GetLocalTime(&t);std::ofstream f(p,std::ios::app);
        f<<std::setfill('0')<<std::setw(4)<<t.wYear<<'-'<<std::setw(2)<<t.wMonth<<'-'<<std::setw(2)<<t.wDay<<'T'<<std::setw(2)<<t.wHour<<':'<<std::setw(2)<<t.wMinute<<':'<<std::setw(2)<<t.wSecond<<' '<<message<<'\n';
    }catch(...){}
}
Config currentConfig(){std::lock_guard guard(configMutex);return config;}
void replaceConfig(Config c){std::lock_guard guard(configMutex);c.generation=config.generation+1;config=std::move(c);}
const Profile& selected(const Config& c,Action action){if(action==Action::Monitor)return c.monitor;if(action==Action::Tv)return c.tv;return c.both;}
Action activeAction(const Config& c){auto actual=capture(query());if(matches(c.monitor,actual))return Action::Monitor;if(matches(c.tv,actual))return Action::Tv;if(matches(c.both,actual))return Action::Both;return Action::None;}
std::string name(Action a){if(a==Action::Monitor)return "monitor";if(a==Action::Tv)return "tv";if(a==Action::Both)return "both";return "other";}
void status(){
    try{auto c=currentConfig();writeJson(root/L"status.json",{{"version","0.1.0"},{"automatic",!paused},{"active",c.ready()?name(activeAction(c)):"unconfigured"},{"network",lastSignal},{"lastError",lastError}});}catch(...){}
}
void launch(const std::wstring& args){
    auto exe=executable();std::wstring line=L"\""+exe+L"\" "+args;STARTUPINFOW s{};s.cb=sizeof(s);s.dwFlags=STARTF_USESHOWWINDOW;s.wShowWindow=SW_HIDE;PROCESS_INFORMATION p{};
    if(!CreateProcessW(exe.c_str(),line.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,root.c_str(),&s,&p))throw std::runtime_error("Cannot launch recovery process");
    CloseHandle(p.hThread);CloseHandle(p.hProcess);
}
void cancelGuards(){for(auto& e:fs::directory_iterator(root))if(e.path().filename().wstring().starts_with(L"guard-")&&e.path().extension()==L".json"){std::error_code ec;fs::remove(e.path(),ec);}}
class SwitchLock {
    HANDLE handle_=nullptr;bool owned_=false;
public:
    SwitchLock(){handle_=CreateMutexW(nullptr,FALSE,(mutexName+L"-switch").c_str());if(!handle_)throw std::runtime_error("Cannot create display lock");DWORD r=WaitForSingleObject(handle_,5000);owned_=r==WAIT_OBJECT_0||r==WAIT_ABANDONED;if(!owned_){CloseHandle(handle_);handle_=nullptr;throw std::runtime_error("Another switch is in progress");}}
    ~SwitchLock(){if(owned_)ReleaseMutex(handle_);if(handle_)CloseHandle(handle_);}
};
void safeApply(const Profile& profile,bool trial=false){
    SwitchLock lock;
    auto previous=capture(query());if(matches(profile,previous)&&!trial)return;
    build(profile); // Validate before starting recovery or changing any screen.
    cancelGuards();
    auto file=root/(L"guard-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".json");
    auto ready=file;ready+=L".ready";
    writeJson(file,{{"previous",saveProfile(previous)},{"seconds",trial?30:10}});
    launch(L"--rollback \""+file.wstring()+L"\"");
    ULONGLONG start=GetTickCount64();while(!fs::exists(ready)&&GetTickCount64()-start<3000)Sleep(20);
    if(!fs::exists(ready)){std::error_code ec;fs::remove(file,ec);throw std::runtime_error("Recovery process did not become ready; switch refused");}
    try{
        apply(profile);
        if(!trial){fs::remove(file);std::error_code ec;fs::remove(ready,ec);}
        log(trial?"Trial applied; independent recovery in 30 seconds":"Profile applied and verified");
    }catch(...){
        try{apply(previous);fs::remove(file);std::error_code ec;fs::remove(ready,ec);log("Previous display profile restored");}catch(...){log("Immediate restoration failed; independent recovery remains armed");}
        throw;
    }
}
void rollback(const fs::path& file){
    if(file.parent_path()!=root||!file.filename().wstring().starts_with(L"guard-"))throw std::runtime_error("Recovery file outside local data directory");
    auto j=readJson(file);auto profile=loadProfile(j.at("previous"));int seconds=j.at("seconds");if(seconds!=10&&seconds!=30)throw std::runtime_error("Invalid recovery deadline");
    build(profile);auto ready=file;ready+=L".ready";writeJson(ready,{{"ready",true}});
    Sleep(seconds*1000);
    SwitchLock lock;
    if(fs::exists(file)){
        try{apply(profile);log("Independent recovery restored previous profile");}catch(const std::exception& e){log(std::string("Independent recovery could not restore: ")+e.what());}
        std::error_code ec;fs::remove(file,ec);
    }
    std::error_code ec;fs::remove(ready,ec);
}
Signal probe(const Config& c){HttpClient client;auto result=client.get(c.address);if(!result.available)return Signal::NoReply;if(result.status!=200)return Signal::Invalid;return parseSignal(result.body,c.model,c.id);}
void manual(Action action,bool trial=false){
    paused=true;if(settingsWindow)settingsPreviouslyPaused=true;controller.reset(Action::None);auto c=currentConfig();
    if(!c.ready())throw std::runtime_error("Open settings and capture display profiles first");
    if(action==Action::Tv&&probe(c)!=Signal::On)throw std::runtime_error("TV is not confirmed on; keeping current display");
    safeApply(selected(c,action),trial);controller.committed(action);log("Manual selection: "+name(action)+"; automation paused");status();
}
void resume(){auto c=currentConfig();if(!c.ready())throw std::runtime_error("Configuration is incomplete");build(c.monitor);build(c.tv);controller.reset(activeAction(c));{std::lock_guard guard(sampleMutex);pending.clear();}paused=false;lastError.clear();wakeWorker.notify_all();log("Automation enabled");status();}
template<class T> struct Com {
    T* p=nullptr;~Com(){if(p)p->Release();}T** out(){return &p;}T* operator->(){return p;}
};
struct Bstr {BSTR p;explicit Bstr(const std::wstring& s):p(SysAllocString(s.c_str())){}~Bstr(){SysFreeString(p);}operator BSTR()const{return p;}};
void hrcheck(HRESULT result){if(FAILED(result))throw std::runtime_error("Windows autostart configuration failed: "+std::to_string((unsigned long)result));}
void autostart(bool enabled){
    Com<ITaskService> service;hrcheck(CoCreateInstance(CLSID_TaskScheduler,nullptr,CLSCTX_INPROC_SERVER,IID_ITaskService,(void**)service.out()));
    VARIANT empty{};VariantInit(&empty);hrcheck(service->Connect(empty,empty,empty,empty));
    Com<ITaskFolder> folder;hrcheck(service->GetFolder(Bstr(L"\\"),folder.out()));
    auto taskName=L"ScreenRelay-"+userSid();
    if(!enabled){HRESULT result=folder->DeleteTask(Bstr(taskName),0);if(FAILED(result)&&result!=HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))hrcheck(result);return;}
    Com<ITaskDefinition> task;hrcheck(service->NewTask(0,task.out()));
    Com<IRegistrationInfo> info;hrcheck(task->get_RegistrationInfo(info.out()));hrcheck(info->put_Description(Bstr(L"ScreenRelay: automatic local Samsung TV / monitor switching")));
    Com<IPrincipal> principal;hrcheck(task->get_Principal(principal.out()));auto sid=userSid();hrcheck(principal->put_UserId(Bstr(sid)));hrcheck(principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN));hrcheck(principal->put_RunLevel(TASK_RUNLEVEL_LUA));
    Com<ITaskSettings> settings;hrcheck(task->get_Settings(settings.out()));hrcheck(settings->put_Hidden(VARIANT_TRUE));hrcheck(settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE));hrcheck(settings->put_StopIfGoingOnBatteries(VARIANT_FALSE));hrcheck(settings->put_ExecutionTimeLimit(Bstr(L"PT0S")));hrcheck(settings->put_RestartCount(3));hrcheck(settings->put_RestartInterval(Bstr(L"PT1M")));hrcheck(settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW));
    Com<ITriggerCollection> triggers;hrcheck(task->get_Triggers(triggers.out()));Com<ITrigger> trigger;hrcheck(triggers->Create(TASK_TRIGGER_LOGON,trigger.out()));Com<ILogonTrigger> logon;hrcheck(trigger->QueryInterface(IID_ILogonTrigger,(void**)logon.out()));hrcheck(logon->put_UserId(Bstr(sid)));
    Com<IActionCollection> actions;hrcheck(task->get_Actions(actions.out()));Com<IAction> action;hrcheck(actions->Create(TASK_ACTION_EXEC,action.out()));Com<IExecAction> exec;hrcheck(action->QueryInterface(IID_IExecAction,(void**)exec.out()));hrcheck(exec->put_Path(Bstr(executable())));hrcheck(exec->put_Arguments(Bstr(L"--auto")));hrcheck(exec->put_WorkingDirectory(Bstr(root.wstring())));
    VARIANT user{};VariantInit(&user);user.vt=VT_BSTR;user.bstrVal=SysAllocString(sid.c_str());Com<IRegisteredTask> registered;
    HRESULT result=folder->RegisterTaskDefinition(Bstr(taskName),task.p,TASK_CREATE_OR_UPDATE,user,empty,TASK_LOGON_INTERACTIVE_TOKEN,empty,registered.out());VariantClear(&user);hrcheck(result);
}
void worker(){
    try {
        HttpClient client;uint64_t sequence=0;
        while(!quitWorker){
            auto start=std::chrono::steady_clock::now();auto c=currentConfig();
            if(c.ready()&&!paused){
                auto result=client.get(c.address);Signal signal=!result.available?Signal::NoReply:(result.status==200?parseSignal(result.body,c.model,c.id):Signal::Invalid);
                bool queued;{std::lock_guard guard(sampleMutex);queued=pending.push({signal,c.generation,++sequence,GetTickCount64()});}
                PostMessageW(mainWindow,WM_SAMPLE,queued?0:2,0);
            }
            std::unique_lock lock(configMutex);wakeWorker.wait_until(lock,start+std::chrono::milliseconds(500),[]{return quitWorker.load();});
        }
    }catch(...){PostMessageW(mainWindow,WM_SAMPLE,1,0);}
}
void messageError(const std::exception& e,bool visible){lastError=e.what();paused=true;if(settingsWindow)settingsPreviouslyPaused=true;log("Paused: "+lastError);status();if(visible)MessageBoxW(settingsWindow,wide(lastError).c_str(),L"ScreenRelay",MB_OK|MB_ICONWARNING);}
HWND control(const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id){
    HWND c=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,settingsWindow,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);
    SendMessageW(c,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);return c;
}
std::wstring field(int id){HWND c=GetDlgItem(settingsWindow,id);int n=GetWindowTextLengthW(c);std::wstring s(n+1,0);GetWindowTextW(c,s.data(),n+1);s.resize(n);return s;}
void refreshChoices(){
    choices.clear();auto all=query(QDC_ALL_PATHS);std::set<std::string> seen;
    for(auto& p:all.paths){if(!p.targetInfo.targetAvailable)continue;try{auto id=identity(p);auto key=id.adapter+"/"+std::to_string(id.technology)+"/"+std::to_string(id.connector);if(seen.insert(key).second)choices.push_back(id);}catch(...){}}
    for(int combo:{MONITOR,TV})SendDlgItemMessageW(settingsWindow,combo,CB_RESETCONTENT,0,0);
    for(auto& d:choices){auto label=wide(d.name+" ("+(d.technology==5?"HDMI":d.technology==10?"DisplayPort":"display")+")");for(int combo:{MONITOR,TV})SendDlgItemMessageW(settingsWindow,combo,CB_ADDSTRING,0,(LPARAM)label.c_str());}
    for(size_t i=0;i<choices.size();++i){if((draft.monitor.size()==1&&choices[i].same(draft.monitor[0].device))||(draft.monitor.empty()&&choices[i].technology==10))SendDlgItemMessageW(settingsWindow,MONITOR,CB_SETCURSEL,i,0);if((draft.tv.size()==1&&choices[i].same(draft.tv[0].device))||(draft.tv.empty()&&choices[i].technology==5))SendDlgItemMessageW(settingsWindow,TV,CB_SETCURSEL,i,0);}
}
void registerKeys(){
    for(int id=1;id<=3;++id)UnregisterHotKey(mainWindow,id);auto c=currentConfig();
    if(!c.ready())return;
    bool ok=true;ok=RegisterHotKey(mainWindow,1,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,c.monitorKey)&&ok;ok=RegisterHotKey(mainWindow,2,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,c.tvKey)&&ok;ok=RegisterHotKey(mainWindow,3,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,c.bothKey)&&ok;
    if(!ok){for(int id=1;id<=3;++id)UnregisterHotKey(mainWindow,id);throw std::runtime_error("A hotkey is unavailable. Change hotkeys in settings; no existing shortcut was taken over.");}
}
LRESULT CALLBACK settingsProc(HWND h,UINT msg,WPARAM w,LPARAM l){
    try {
        if(msg==WM_COMMAND){
            switch(LOWORD(w)){
            case REFRESH:refreshChoices();return 0;
            case TEST:{
                auto address=utf8(field(IP));if(!localAddress(address))throw std::runtime_error("Enter the TV's private IPv4 address, for example 192.168.1.50");
                HttpClient client;auto response=client.get(address);if(!response.available||response.status!=200)throw std::runtime_error("TV did not respond. Turn it on and check its IP address.");
                auto device=json::parse(response.body).at("device");if(device.at("type")!="Samsung SmartTV")throw std::runtime_error("This endpoint is not a Samsung Smart TV");
                draft.address=address;draft.model=device.at("modelName");draft.id=device.at("id");
                if(draft.model.empty()||draft.id.empty())throw std::runtime_error("TV did not provide a reliable identity");
                SetDlgItemTextW(h,INFO,wide("Verified TV: "+draft.model).c_str());return 0;
            }
        case CAPTURE:{
                int m=(int)SendDlgItemMessageW(h,MONITOR,CB_GETCURSEL,0,0),t=(int)SendDlgItemMessageW(h,TV,CB_GETCURSEL,0,0);
                if(m<0||t<0||m==t)throw std::runtime_error("Select two different displays");
                auto both=capture(query());if(both.size()!=2)throw std::runtime_error("Use Win+P > Extend first. Set your preferred refresh rates in Windows, then capture.");
                draft.monitor.clear();draft.tv.clear();
                for(auto e:both){if(e.device.same(choices.at(m)))draft.monitor={e};if(e.device.same(choices.at(t)))draft.tv={e};}
                if(draft.monitor.empty()||draft.tv.empty())throw std::runtime_error("Selected devices do not match the two active displays");
                draft.both=both;draft.monitorVerified=draft.tvVerified=false;build(draft.monitor);build(draft.tv);build(draft.both);
                SetDlgItemTextW(h,INFO,L"Profiles captured. Test and confirm both single-display modes before saving.");return 0;
            }
            case TRIAL_MONITOR:
            case TRIAL_TV:{
                bool tv=LOWORD(w)==TRIAL_TV;const auto& profile=tv?draft.tv:draft.monitor;
                if(profile.size()!=1||draft.both.size()!=2)throw std::runtime_error("Capture profiles first");
                if(tv&&probe(draft)!=Signal::On)throw std::runtime_error("Test the TV address and turn the TV on before its display trial");
                safeApply(profile,true);
                int answer=MessageBoxW(h,L"Do you see this message on the selected single display?\n\nConfirm within 30 seconds. Otherwise both displays return automatically.",L"ScreenRelay display test",MB_YESNO|MB_ICONQUESTION);
                bool confirmed=answer==IDYES&&matches(profile,capture(query()));
                safeApply(draft.both);
                if(tv)draft.tvVerified=confirmed;else draft.monitorVerified=confirmed;
                SetDlgItemTextW(h,INFO,confirmed?L"Single-display picture confirmed. Test the other display, then save.":L"Trial not confirmed. Both displays restored.");return 0;
            }
            case SAVE:{
                if(!draft.ready()||draft.address!=utf8(field(IP)))throw std::runtime_error("Test the TV address and capture profiles before saving");
                draft.startup=SendDlgItemMessageW(h,STARTUP,BM_GETCHECK,0,0)==BST_CHECKED;
                for(auto pair:{std::pair{110,&draft.bothKey},std::pair{111,&draft.monitorKey},std::pair{112,&draft.tvKey}}){int index=(int)SendDlgItemMessageW(h,pair.first,CB_GETCURSEL,0,0);if(index<0)throw std::runtime_error("Select all three hotkeys");*pair.second=VK_F1+index;}
                auto checked=Config::load(draft.save());build(checked.monitor);build(checked.tv);build(checked.both);
                auto old=currentConfig();replaceConfig(checked);
                try{registerKeys();autostart(checked.startup);writeJson(root/L"config.json",checked.save());}catch(...){replaceConfig(old);try{registerKeys();autostart(old.startup);}catch(...){}throw;}
                settingsPreviouslyPaused=false;DestroyWindow(h);resume();return 0;
            }
            }
        }
        if(msg==WM_CLOSE){paused=settingsPreviouslyPaused;DestroyWindow(h);status();return 0;}
        if(msg==WM_DESTROY){settingsWindow=nullptr;return 0;}
    }catch(const std::exception& e){messageError(e,true);return 0;}
    return DefWindowProcW(h,msg,w,l);
}
void showSettings(){
    if(settingsWindow){ShowWindow(settingsWindow,SW_SHOW);SetForegroundWindow(settingsWindow);return;}
    settingsPreviouslyPaused=paused.load();paused=true;draft=currentConfig();
    WNDCLASSW cls{};cls.lpfnWndProc=settingsProc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"ScreenRelaySettings";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);RegisterClassW(&cls);
    settingsWindow=CreateWindowExW(WS_EX_DLGMODALFRAME,cls.lpszClassName,L"ScreenRelay — settings",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU, CW_USEDEFAULT,CW_USEDEFAULT,560,480,nullptr,nullptr,cls.hInstance,nullptr);
    control(L"STATIC",L"Samsung TV IPv4 address",0,20,18,240,20,0);control(L"EDIT",wide(draft.address).c_str(),WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,20,42,285,24,IP);control(L"BUTTON",L"Test TV",BS_PUSHBUTTON|WS_TABSTOP,325,40,180,28,TEST);
    control(L"STATIC",L"Desk monitor",0,20,78,220,18,0);control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,20,100,485,180,MONITOR);
    control(L"STATIC",L"Television display",0,20,135,220,18,0);control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,20,157,485,180,TV);
    control(L"BUTTON",L"Refresh list",BS_PUSHBUTTON|WS_TABSTOP,20,193,135,28,REFRESH);control(L"BUTTON",L"Capture current Extended profiles",BS_PUSHBUTTON|WS_TABSTOP,170,193,335,28,CAPTURE);
    control(L"BUTTON",L"Start automatically when I sign in",BS_AUTOCHECKBOX|WS_TABSTOP,20,232,485,24,STARTUP);SendDlgItemMessageW(settingsWindow,STARTUP,BM_SETCHECK,draft.startup?BST_CHECKED:BST_UNCHECKED,0);
    control(L"STATIC",L"Ctrl+Alt hotkeys: Both / Monitor / TV",0,20,268,485,20,0);
    for(auto pair:{std::pair{110,draft.bothKey},std::pair{111,draft.monitorKey},std::pair{112,draft.tvKey}}){int x=20+(pair.first-110)*165;control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,x,292,155,200,pair.first);for(int f=1;f<=24;++f){auto label=L"F"+std::to_wstring(f);SendDlgItemMessageW(settingsWindow,pair.first,CB_ADDSTRING,0,(LPARAM)label.c_str());}SendDlgItemMessageW(settingsWindow,pair.first,CB_SETCURSEL,pair.second-VK_F1,0);}
    control(L"BUTTON",L"Test monitor picture",BS_PUSHBUTTON|WS_TABSTOP,20,330,230,28,TRIAL_MONITOR);control(L"BUTTON",L"Test TV picture",BS_PUSHBUTTON|WS_TABSTOP,270,330,235,28,TRIAL_TV);
    control(L"STATIC",lastError.empty()?L"No tray icon. Manual hotkeys pause automation.":wide(lastError).c_str(),0,20,368,485,35,INFO);control(L"BUTTON",L"Save and start automation",BS_DEFPUSHBUTTON|WS_TABSTOP,240,408,265,28,SAVE);
    refreshChoices();ShowWindow(settingsWindow,SW_SHOW);SetForegroundWindow(settingsWindow);status();
}
void handleCommand(unsigned command){if(command==1)resume();else if(command==2)manual(Action::Monitor);else if(command==3)manual(Action::Tv);else if(command==4)manual(Action::Both);else if(command==5){paused=true;if(settingsWindow)settingsPreviouslyPaused=true;log("Automation paused");status();}else if(command==6)PostQuitMessage(0);else if(command==7)showSettings();else if(command==8)status();}
LRESULT CALLBACK mainProc(HWND h,UINT msg,WPARAM w,LPARAM l){
    try{
        if(msg==WM_SAMPLE){
            if(w){throw std::runtime_error("Network worker stopped unexpectedly");}
            std::optional<Sample> next;{std::lock_guard guard(sampleMutex);next=pending.take();}if(!next)return 0;auto sample=*next;
            auto c=currentConfig();if(sample.generation!=c.generation)return 0;
            if(GetTickCount64()-sample.time>1500&&sample.signal!=Signal::Invalid){controller.reset(activeAction(c));return 0;}
            std::string signal=sample.signal==Signal::On?"on":sample.signal==Signal::Off?"off":sample.signal==Signal::Invalid?"invalid":"no-reply";
            if(signal!=lastSignal){lastSignal=signal;log("TV signal: "+signal);status();}
            if(!paused){auto action=controller.feed(sample.signal);if(action==Action::Pause)throw std::runtime_error("Unexpected TV identity or response; automation paused");if(action!=Action::None){safeApply(selected(c,action));controller.committed(action);log("Automatic selection: "+name(action));status();}}
            return 0;
        }
        if(msg==WM_HOTKEY){handleCommand(w==1?2:w==2?3:4);return 0;}
        if(msg==WM_COMMAND_REMOTE){handleCommand((unsigned)w);return 0;}
        if(msg==WM_POWERBROADCAST){if(w==PBT_APMRESUMEAUTOMATIC||w==PBT_APMRESUMESUSPEND){auto c=currentConfig();if(c.ready())controller.reset(activeAction(c));log("Resumed; display identifiers will be rebound before next switch");}return TRUE;}
        if(msg==WM_DISPLAYCHANGE){auto c=currentConfig();if(c.ready())controller.reset(activeAction(c));status();return 0;}
    }catch(const std::exception& e){messageError(e,msg==WM_COMMAND_REMOTE||msg==WM_HOTKEY);return 0;}
    return DefWindowProcW(h,msg,w,l);
}
unsigned commandNumber(const std::wstring& arg){if(arg==L"--auto")return 1;if(arg==L"--monitor")return 2;if(arg==L"--tv")return 3;if(arg==L"--both")return 4;if(arg==L"--pause")return 5;if(arg==L"--exit")return 6;if(arg==L"--settings")return 7;if(arg==L"--status")return 8;return 0;}
int wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
    std::thread poller;HANDLE singleton=nullptr;
    try{
        wchar_t local[MAX_PATH]{};wincheck(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,local),"Locate local application data");root=fs::path(local)/L"ScreenRelay";fs::create_directories(root);
        auto sid=userSid();mutexName=L"Local\\ScreenRelay-"+sid;className=L"ScreenRelay-"+sid;
        CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);std::vector<std::wstring> args;for(int i=1;i<argc;++i)args.emplace_back(argv[i]);LocalFree(argv);
        if(args.size()==2&&args[0]==L"--rollback"){rollback(fs::path(args[1]));CoUninitialize();return 0;}
        try{if(fs::exists(root/L"config.json"))config=Config::load(readJson(root/L"config.json"));}catch(const std::exception& e){lastError=e.what();log("Settings require repair");}
        if(args.size()==3&&args[0]==L"--import-prototype"){
            Config c;c.tvKey=VK_F12;fs::path folder=args[1];c.address=utf8(args[2]);c.monitor=importPrototype(readJson(folder/L"Redmi.json"));c.tv=importPrototype(readJson(folder/L"TV.json"));c.both=importPrototype(readJson(folder/L"Extended.json"));
            c.monitorVerified=fs::exists(folder/L"verified-Redmi.txt");c.tvVerified=fs::exists(folder/L"verified-TV.txt");if(!c.monitorVerified||!c.tvVerified)throw std::runtime_error("Prototype must have both visual trials confirmed before import");
            HttpClient client;auto response=client.get(c.address);if(!response.available||response.status!=200)throw std::runtime_error("Turn TV on before importing profiles");auto d=json::parse(response.body).at("device");if(d.at("type")!="Samsung SmartTV")throw std::runtime_error("Not a Samsung television");c.model=d.at("modelName");c.id=d.at("id");c=Config::load(c.save());build(c.monitor);build(c.tv);build(c.both);writeJson(root/L"config.json",c.save());log("Prototype profiles imported; no displays changed");return 0;
        }
        if(!args.empty()&&args[0]==L"--diagnose"){
            json report={{"version","0.1.0"},{"displays",json::array()}};for(auto& e:capture(query()))report["displays"].push_back({{"name",e.device.name},{"width",e.source.sourceMode.width},{"height",e.source.sourceMode.height},{"refreshNumerator",e.refresh.Numerator},{"refreshDenominator",e.refresh.Denominator}});
            if(config.ready()){build(config.monitor);build(config.tv);build(config.both);report["profileValidation"]="passed";report["network"]=probe(config)==Signal::On?"on":"not-on";}writeJson(root/L"diagnostics.json",report);return 0;
        }
        if(!args.empty()&&args[0]==L"--startup-on"){if(!config.ready())throw std::runtime_error("Complete setup before enabling autostart");autostart(true);config.startup=true;writeJson(root/L"config.json",config.save());return 0;}
        if(!args.empty()&&args[0]==L"--startup-off"){autostart(false);if(config.ready()){config.startup=false;writeJson(root/L"config.json",config.save());}return 0;}
        unsigned command=args.empty()?1:commandNumber(args[0]);
        singleton=CreateMutexW(nullptr,TRUE,mutexName.c_str());if(!singleton)throw std::runtime_error("Cannot create single-instance lock");bool existing=GetLastError()==ERROR_ALREADY_EXISTS;
        if(existing){HWND target=nullptr;for(int retry=0;retry<30&&!target;++retry){target=FindWindowW(className.c_str(),nullptr);if(!target)Sleep(100);}if(!target)throw std::runtime_error("Running instance did not respond");PostMessageW(target,WM_COMMAND_REMOTE,command?command:7,0);CloseHandle(singleton);return 0;}
        WNDCLASSW cls{};cls.lpfnWndProc=mainProc;cls.hInstance=instance;cls.lpszClassName=className.c_str();RegisterClassW(&cls);
        mainWindow=CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,L"ScreenRelay",WS_POPUP,0,0,0,0,nullptr,nullptr,instance,nullptr);if(!mainWindow)throw std::runtime_error("Cannot create hidden command window");
        if(!args.empty()&&args[0]==L"--trial-tv"){manual(Action::Tv,true);CloseHandle(singleton);return 0;}
        if(!args.empty()&&args[0]==L"--trial-monitor"){manual(Action::Monitor,true);CloseHandle(singleton);return 0;}
        try{registerKeys();}catch(const std::exception& e){messageError(e,false);command=7;}
        poller=std::thread(worker);
        if(!config.ready())showSettings();else{try{handleCommand(command?command:7);}catch(const std::exception& e){messageError(e,command!=1);}}
        MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(!settingsWindow||!IsDialogMessageW(settingsWindow,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
        quitWorker=true;wakeWorker.notify_all();poller.join();for(int id=1;id<=3;++id)UnregisterHotKey(mainWindow,id);DestroyWindow(mainWindow);CloseHandle(singleton);log("Stopped");CoUninitialize();return 0;
    }catch(const std::exception& e){
        quitWorker=true;wakeWorker.notify_all();if(poller.joinable())poller.join();if(singleton)CloseHandle(singleton);log(std::string("Startup failed: ")+e.what());
        // Explicit command invocations report failure in local status, not background popups.
        try{writeJson(root/L"status.json",{{"automatic",false},{"lastError",e.what()}});}catch(...){}return 1;
    }
}
