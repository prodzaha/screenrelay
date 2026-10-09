#pragma once
#include "display.hpp"
#include <taskschd.h>
#include <functional>
namespace relay {
template<class T> struct ComPtr {
    T* p=nullptr;
    ComPtr()=default;ComPtr(const ComPtr&)=delete;ComPtr& operator=(const ComPtr&)=delete;
    ~ComPtr(){if(p)p->Release();}
    T** out(){return &p;}T* operator->()const{return p;}
};
struct Bstr {
    BSTR p;
    explicit Bstr(const std::wstring& s):p(SysAllocString(s.c_str())){if(!p)throw std::bad_alloc();}
    Bstr(const Bstr&)=delete;
    ~Bstr(){SysFreeString(p);}operator BSTR()const{return p;}
};
inline void checkHr(HRESULT result){if(FAILED(result))throw std::runtime_error("Windows startup task failed: "+std::to_string((unsigned long)result));}
inline void deleteTask(ITaskFolder* folder,const std::wstring& name){auto result=folder->DeleteTask(Bstr(name),0);if(result!=HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))checkHr(result);}
inline void removeLegacyStartup(){
    HKEY key=nullptr;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_SET_VALUE,&key)==ERROR_SUCCESS){RegDeleteValueW(key,L"ScreenRelay");RegCloseKey(key);}
}
inline void setAutostart(bool enabled,const std::wstring& exe,const std::wstring& directory,const std::wstring& sid,const std::wstring& nonce,const std::function<bool()>& probeReady){
    ComPtr<ITaskService> service;
    checkHr(CoCreateInstance(CLSID_TaskScheduler,nullptr,CLSCTX_INPROC_SERVER,IID_ITaskService,(void**)service.out()));
    VARIANT empty{};VariantInit(&empty);checkHr(service->Connect(empty,empty,empty,empty));
    ComPtr<ITaskFolder> folder;checkHr(service->GetFolder(Bstr(L"\\"),folder.out()));
    const auto name=L"ScreenRelay-"+sid,probeName=L"ScreenRelay-launchcheck-"+sid;
    if(!enabled){deleteTask(folder.p,name);removeLegacyStartup();return;}
    ComPtr<ITaskDefinition> task;checkHr(service->NewTask(0,task.out()));
    ComPtr<IRegistrationInfo> info;checkHr(task->get_RegistrationInfo(info.out()));checkHr(info->put_Description(Bstr(L"ScreenRelay: automatic local TV and monitor switching")));
    ComPtr<IPrincipal> principal;checkHr(task->get_Principal(principal.out()));checkHr(principal->put_UserId(Bstr(sid)));checkHr(principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN));checkHr(principal->put_RunLevel(TASK_RUNLEVEL_LUA));
    ComPtr<ITaskSettings> settings;checkHr(task->get_Settings(settings.out()));
    checkHr(settings->put_Hidden(VARIANT_TRUE));checkHr(settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE));checkHr(settings->put_StopIfGoingOnBatteries(VARIANT_FALSE));
    checkHr(settings->put_ExecutionTimeLimit(Bstr(L"PT0S")));checkHr(settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW));
    ComPtr<IActionCollection> actions;checkHr(task->get_Actions(actions.out()));ComPtr<IAction> action;checkHr(actions->Create(TASK_ACTION_EXEC,action.out()));
    ComPtr<IExecAction> exec;checkHr(action->QueryInterface(IID_IExecAction,(void**)exec.out()));checkHr(exec->put_Path(Bstr(exe)));checkHr(exec->put_WorkingDirectory(Bstr(directory)));
    checkHr(exec->put_Arguments(Bstr(L"--startup-probe "+nonce)));
    auto registerTask=[&](const std::wstring& taskName,ComPtr<IRegisteredTask>& result){
        VARIANT user{};user.vt=VT_BSTR;user.bstrVal=SysAllocString(sid.c_str());
        auto hr=folder->RegisterTaskDefinition(Bstr(taskName),task.p,TASK_CREATE_OR_UPDATE,user,empty,TASK_LOGON_INTERACTIVE_TOKEN,empty,result.out());VariantClear(&user);checkHr(hr);
    };
    try {
        ComPtr<IRegisteredTask> probe;registerTask(probeName,probe);ComPtr<IRunningTask> running;checkHr(probe->Run(empty,running.out()));
        auto start=GetTickCount64();bool ready=false;
        while(GetTickCount64()-start<10000){if(probeReady()){ready=true;break;}Sleep(50);}
        if(!ready){running->Stop();throw std::runtime_error("Windows could not launch the installed EXE; startup not enabled");}
        deleteTask(folder.p,probeName);
    }catch(...){try{deleteTask(folder.p,probeName);}catch(...){}throw;}
    checkHr(exec->put_Arguments(Bstr(L"--auto")));
    ComPtr<ITriggerCollection> triggers;checkHr(task->get_Triggers(triggers.out()));ComPtr<ITrigger> trigger;checkHr(triggers->Create(TASK_TRIGGER_LOGON,trigger.out()));
    ComPtr<ILogonTrigger> logon;checkHr(trigger->QueryInterface(IID_ILogonTrigger,(void**)logon.out()));checkHr(logon->put_UserId(Bstr(sid)));
    ComPtr<IRegisteredTask> registered;registerTask(name,registered);removeLegacyStartup();
}
}
