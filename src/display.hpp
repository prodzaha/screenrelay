#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "../vendor/json.hpp"
namespace relay {
using json=nlohmann::json;
inline std::string utf8(const std::wstring& s) {
    if(s.empty())return {};
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);
    std::string r(n,0);WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),r.data(),n,nullptr,nullptr);return r;
}
inline std::wstring wide(const std::string& s) {
    if(s.empty())return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0);
    if(!n)throw std::runtime_error("Invalid UTF-8");
    std::wstring r(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),r.data(),n);return r;
}
inline void wincheck(LONG code,const char* operation) {
    if(code!=ERROR_SUCCESS)throw std::runtime_error(std::string(operation)+": Windows error "+std::to_string(code));
}
struct Snapshot { std::vector<DISPLAYCONFIG_PATH_INFO> paths; std::vector<DISPLAYCONFIG_MODE_INFO> modes; };
inline Snapshot query(UINT32 flags=QDC_ONLY_ACTIVE_PATHS) {
    for(int retry=0;retry<4;++retry) {
        UINT32 p=0,m=0;wincheck(GetDisplayConfigBufferSizes(flags,&p,&m),"Display buffer sizes");
        Snapshot s;s.paths.resize(p);s.modes.resize(m);
        LONG result=QueryDisplayConfig(flags,&p,s.paths.data(),&m,s.modes.data(),nullptr);
        if(result==ERROR_INSUFFICIENT_BUFFER)continue;
        wincheck(result,"Read display configuration");s.paths.resize(p);s.modes.resize(m);return s;
    }
    throw std::runtime_error("Display configuration is changing; retry when stable");
}
struct Identity {
    std::string adapter,name;
    UINT32 technology=0,connector=0;
    UINT16 manufacturer=0;
    bool same(const Identity& other)const {
        return adapter==other.adapter && technology==other.technology && connector==other.connector && manufacturer==other.manufacturer;
    }
    json save()const {return {{"adapter",adapter},{"name",name},{"technology",technology},{"connector",connector},{"manufacturer",manufacturer}};}
    static Identity load(const json& j){return {j.at("adapter"),j.at("name"),j.at("technology"),j.at("connector"),j.at("manufacturer")};}
};
inline Identity identity(const DISPLAYCONFIG_PATH_INFO& p) {
    DISPLAYCONFIG_TARGET_DEVICE_NAME target{};target.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    target.header.size=sizeof(target);target.header.adapterId=p.targetInfo.adapterId;target.header.id=p.targetInfo.id;
    wincheck(DisplayConfigGetDeviceInfo(&target.header),"Read display identity");
    if(!target.flags.edidIdsValid)throw std::runtime_error("Display has no reliable EDID identity");
    DISPLAYCONFIG_ADAPTER_NAME adapter{};adapter.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME;
    adapter.header.size=sizeof(adapter);adapter.header.adapterId=p.targetInfo.adapterId;
    wincheck(DisplayConfigGetDeviceInfo(&adapter.header),"Read GPU identity");
    return {utf8(adapter.adapterDevicePath),utf8(target.monitorFriendlyDeviceName),(UINT32)target.outputTechnology,target.connectorInstance,target.edidManufactureId};
}
inline std::string encode(const void* data,size_t size) {
    constexpr char digits[]="0123456789abcdef";std::string out(size*2,0);auto b=(const unsigned char*)data;
    for(size_t i=0;i<size;++i){out[i*2]=digits[b[i]>>4];out[i*2+1]=digits[b[i]&15];}return out;
}
template<class T> inline T decode(const std::string& text) {
    if(text.size()!=sizeof(T)*2)throw std::runtime_error("Unsupported profile data length");
    auto digit=[](char c)->unsigned {if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;throw std::runtime_error("Invalid profile data");};
    T out{};auto b=(unsigned char*)&out;for(size_t i=0;i<sizeof(T);++i)b[i]=(unsigned char)((digit(text[2*i])<<4)|digit(text[2*i+1]));return out;
}
struct Entry {
    Identity device;
    DISPLAYCONFIG_MODE_INFO source{},target{};
    DISPLAYCONFIG_RATIONAL refresh{};
    DISPLAYCONFIG_ROTATION rotation{};
    DISPLAYCONFIG_SCALING scaling{};
    DISPLAYCONFIG_SCANLINE_ORDERING scanline{};
    json save()const { return {{"device",device.save()},{"source",encode(&source,sizeof(source))},{"target",encode(&target,sizeof(target))},
        {"refreshNumerator",refresh.Numerator},{"refreshDenominator",refresh.Denominator},{"rotation",(int)rotation},{"scaling",(int)scaling},{"scanline",(int)scanline}}; }
    static Entry load(const json& j) {
        Entry e;e.device=Identity::load(j.at("device"));e.source=decode<DISPLAYCONFIG_MODE_INFO>(j.at("source"));e.target=decode<DISPLAYCONFIG_MODE_INFO>(j.at("target"));
        e.refresh={j.at("refreshNumerator"),j.at("refreshDenominator")};e.rotation=(DISPLAYCONFIG_ROTATION)j.at("rotation").get<int>();
        e.scaling=(DISPLAYCONFIG_SCALING)j.at("scaling").get<int>();e.scanline=(DISPLAYCONFIG_SCANLINE_ORDERING)j.at("scanline").get<int>();
        if(e.source.infoType!=DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE || e.target.infoType!=DISPLAYCONFIG_MODE_INFO_TYPE_TARGET || !e.refresh.Denominator || !e.source.sourceMode.width || !e.source.sourceMode.height)
            throw std::runtime_error("Invalid saved display mode");
        return e;
    }
};
using Profile=std::vector<Entry>;
inline Profile capture(const Snapshot& s) {
    Profile out;
    for(auto& p:s.paths) {
        if(p.sourceInfo.modeInfoIdx>=s.modes.size() || p.targetInfo.modeInfoIdx>=s.modes.size())throw std::runtime_error("Exact display modes unavailable");
        out.push_back({identity(p),s.modes[p.sourceInfo.modeInfoIdx],s.modes[p.targetInfo.modeInfoIdx],p.targetInfo.refreshRate,p.targetInfo.rotation,p.targetInfo.scaling,p.targetInfo.scanLineOrdering});
    }
    return out;
}
inline json saveProfile(const Profile& p){json j=json::array();for(auto& e:p)j.push_back(e.save());return j;}
inline Profile loadProfile(const json& j){if(!j.is_array()||j.empty()||j.size()>2)throw std::runtime_error("Profile requires one or two displays");Profile p;for(auto& e:j)p.push_back(Entry::load(e));return p;}
inline LONG validate(const Snapshot& s) {
    return SetDisplayConfig((UINT32)s.paths.size(),const_cast<DISPLAYCONFIG_PATH_INFO*>(s.paths.data()),(UINT32)s.modes.size(),const_cast<DISPLAYCONFIG_MODE_INFO*>(s.modes.data()),SDC_VALIDATE|SDC_USE_SUPPLIED_DISPLAY_CONFIG);
}
inline Snapshot build(const Profile& profile) {
    if(profile.empty() || profile.size()>2)throw std::runtime_error("Invalid profile display count");
    auto live=query(QDC_ALL_PATHS);
    std::vector<std::vector<DISPLAYCONFIG_PATH_INFO>> options(profile.size());
    for(auto p:live.paths) {
        if(!p.targetInfo.targetAvailable)continue;
        Identity id;try{id=identity(p);}catch(...){continue;}
        for(size_t i=0;i<profile.size();++i)if(id.same(profile[i].device))options[i].push_back(p);
    }
    for(auto& list:options) {
        if(list.empty())throw std::runtime_error("Configured display is unavailable; keeping current screen");
        std::stable_sort(list.begin(),list.end(),[](auto& a,auto& b){return (a.flags&DISPLAYCONFIG_PATH_ACTIVE)>(b.flags&DISPLAYCONFIG_PATH_ACTIVE);});
        std::set<UINT32> targets;for(auto& p:list)targets.insert(p.targetInfo.id);
        if(targets.size()!=1)throw std::runtime_error("Display identity is ambiguous; select devices again");
    }
    auto candidate=[&](size_t a,size_t b) {
        Snapshot s;
        for(size_t i=0;i<profile.size();++i) {
            auto p=options[i][i==0?a:b];auto& entry=profile[i];
            p.flags=DISPLAYCONFIG_PATH_ACTIVE;p.sourceInfo.statusFlags=0;p.targetInfo.statusFlags=0;
            p.sourceInfo.modeInfoIdx=(UINT32)s.modes.size();p.targetInfo.modeInfoIdx=(UINT32)s.modes.size()+1;
            p.targetInfo.refreshRate=entry.refresh;p.targetInfo.rotation=entry.rotation;p.targetInfo.scaling=entry.scaling;p.targetInfo.scanLineOrdering=entry.scanline;
            auto source=entry.source,target=entry.target;
            source.adapterId=p.sourceInfo.adapterId;source.id=p.sourceInfo.id;target.adapterId=p.targetInfo.adapterId;target.id=p.targetInfo.id;
            if(profile.size()==1)source.sourceMode.position={0,0};
            s.paths.push_back(p);s.modes.push_back(source);s.modes.push_back(target);
        }
        return s;
    };
    size_t second=profile.size()==2?options[1].size():1;
    for(size_t a=0;a<options[0].size();++a)for(size_t b=0;b<second;++b) {
        if(profile.size()==2 && options[0][a].sourceInfo.id==options[1][b].sourceInfo.id &&
            !std::memcmp(&options[0][a].sourceInfo.adapterId,&options[1][b].sourceInfo.adapterId,sizeof(LUID)))continue;
        auto s=candidate(a,b);if(validate(s)==ERROR_SUCCESS)return s;
    }
    throw std::runtime_error("Windows rejected the exact saved modes; no display was changed");
}
inline bool matches(const Profile& desired,const Profile& actual) {
    if(desired.size()!=actual.size())return false;
    for(auto& want:desired) {
        auto it=std::find_if(actual.begin(),actual.end(),[&](auto& a){return a.device.same(want.device);});
        if(it==actual.end() || it->source.sourceMode.width!=want.source.sourceMode.width || it->source.sourceMode.height!=want.source.sourceMode.height ||
            (uint64_t)it->refresh.Numerator*want.refresh.Denominator!=(uint64_t)want.refresh.Numerator*it->refresh.Denominator || it->rotation!=want.rotation)return false;
        LONG x=desired.size()==1?0:want.source.sourceMode.position.x,y=desired.size()==1?0:want.source.sourceMode.position.y;
        if(it->source.sourceMode.position.x!=x||it->source.sourceMode.position.y!=y)return false;
    }
    return true;
}
inline bool consistentProfiles(const Profile& monitor,const Profile& tv,const Profile& both) {
    if(monitor.size()!=1||tv.size()!=1||both.size()!=2||monitor[0].device.same(tv[0].device)||both[0].device.same(both[1].device))return false;
    for(const auto* single:{&monitor,&tv}) {
        auto found=std::find_if(both.begin(),both.end(),[&](const auto& e){return e.device.same((*single)[0].device);});
        if(found==both.end())return false;
        auto entry=*found;entry.source.sourceMode.position={0,0};
        if(!matches(*single,{entry}))return false;
    }
    return true;
}
inline void apply(const Profile& profile) {
    if(matches(profile,capture(query())))return;
    auto s=build(profile);
    wincheck(SetDisplayConfig((UINT32)s.paths.size(),s.paths.data(),(UINT32)s.modes.size(),s.modes.data(),SDC_APPLY|SDC_USE_SUPPLIED_DISPLAY_CONFIG),"Apply exact display profile");
    if(!matches(profile,capture(query())))throw std::runtime_error("Display verification failed after application");
}
inline std::vector<unsigned char> base64(const std::string& text) {
    DWORD n=0;if(!CryptStringToBinaryA(text.c_str(),(DWORD)text.size(),CRYPT_STRING_BASE64,nullptr,&n,nullptr,nullptr))throw std::runtime_error("Invalid prototype profile");
    std::vector<unsigned char>b(n);if(!CryptStringToBinaryA(text.c_str(),(DWORD)text.size(),CRYPT_STRING_BASE64,b.data(),&n,nullptr,nullptr))throw std::runtime_error("Invalid prototype profile");return b;
}
inline Profile importPrototype(const json& j) {
    auto p=base64(j.at("Paths")),m=base64(j.at("Modes"));
    if(p.empty()||p.size()%sizeof(DISPLAYCONFIG_PATH_INFO)||m.empty()||m.size()%sizeof(DISPLAYCONFIG_MODE_INFO))throw std::runtime_error("Unsupported prototype profile");
    Snapshot s;s.paths.resize(p.size()/sizeof(DISPLAYCONFIG_PATH_INFO));s.modes.resize(m.size()/sizeof(DISPLAYCONFIG_MODE_INFO));
    std::memcpy(s.paths.data(),p.data(),p.size());std::memcpy(s.modes.data(),m.data(),m.size());return capture(s);
}
}
