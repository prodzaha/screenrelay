#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include "display.hpp"
#include "controller.hpp"
#include <winhttp.h>
#include <atomic>
#include <memory>
#include <mutex>
namespace relay {
inline std::mutex httpMutex;
inline bool localAddress(const std::string& address) {
    IN_ADDR parsed{};
    if(InetPtonA(AF_INET,address.c_str(),&parsed)!=1)return false;
    unsigned n=ntohl(parsed.S_un.S_addr);
    return (n>>24)==10 || (n>>20)==0xac1 || (n>>16)==0xc0a8 || (n>>16)==0xa9fe;
}
inline Signal parseSignal(const std::string& body,const std::string& model,const std::string& id) {
    try {
        auto j=json::parse(body);auto& d=j.at("device");
        if(d.at("modelName").get<std::string>()!=model || d.at("id").get<std::string>()!=id || d.at("type").get<std::string>()!="Samsung SmartTV")return Signal::Invalid;
        auto power=d.at("PowerState").get<std::string>();
        if(power=="on")return Signal::On;if(power=="off")return Signal::Off;
    }catch(...){}
    return Signal::Invalid;
}
struct HttpResult { bool available=false; DWORD status=0; std::string body; };
class HttpClient {
    struct Request {
        HANDLE done=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        std::atomic<int> references=2;
        std::recursive_mutex mutex;
        std::atomic<bool> closing=false;
        bool available=false;
        DWORD status=0;
        char buffer[16384]{};
        std::string body;
        void release(){if(--references==0)delete this;}
        ~Request(){if(done)CloseHandle(done);}
    };
    HINTERNET session_=nullptr,connection_=nullptr;
    std::string host_;
    static void fail(Request* r){SetEvent(r->done);}
    static void CALLBACK callback(HINTERNET request,DWORD_PTR context,DWORD status,void* information,DWORD length) noexcept {
        auto r=reinterpret_cast<Request*>(context);if(!r)return;
        if(status==WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING){r->release();return;}
        std::lock_guard guard(r->mutex);
        if(r->closing)return;
        try {
            switch(status) {
            case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
                if(!WinHttpReceiveResponse(request,nullptr))fail(r);break;
            case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE: {
                DWORD n=sizeof(r->status);
                if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&r->status,&n,WINHTTP_NO_HEADER_INDEX)){fail(r);break;}
                r->available=true;
                if(r->status!=200){SetEvent(r->done);break;}
                if(!WinHttpQueryDataAvailable(request,nullptr))fail(r);break;
            }
            case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE: {
                if(length!=sizeof(DWORD)){fail(r);break;}
                DWORD available=*static_cast<DWORD*>(information);
                if(!available){SetEvent(r->done);break;}
                if(r->body.size()>=65536){r->status=413;SetEvent(r->done);break;}
                DWORD count=std::min<DWORD>(available,sizeof(r->buffer));
                if(!WinHttpReadData(request,r->buffer,count,nullptr))fail(r);break;
            }
            case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
                r->body.append(static_cast<char*>(information),length);
                if(r->body.size()>65536){r->status=413;SetEvent(r->done);break;}
                if(!WinHttpQueryDataAvailable(request,nullptr))fail(r);break;
            case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR: r->available=false;fail(r);break;
            }
        }catch(...){r->available=false;fail(r);}
    }
public:
    HttpClient(){
        session_=WinHttpOpen(L"ScreenRelay/0.1.0",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC);
        if(!session_)throw std::runtime_error("Cannot create local HTTP client");
        WinHttpSetTimeouts(session_,200,200,200,200);
    }
    ~HttpClient(){if(connection_)WinHttpCloseHandle(connection_);if(session_)WinHttpCloseHandle(session_);}
    HttpClient(const HttpClient&)=delete;HttpClient& operator=(const HttpClient&)=delete;
    HttpResult get(const std::string& host) {
        std::lock_guard serial(httpMutex);
        if(!localAddress(host))return {true,400,{}};
        if(host!=host_){if(connection_)WinHttpCloseHandle(connection_);connection_=WinHttpConnect(session_,wide(host).c_str(),8001,0);host_=host;}
        if(!connection_)return {};
        HINTERNET request=WinHttpOpenRequest(connection_,L"GET",L"/api/v2/",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0);
        if(!request)return {};
        auto state=new Request;
        if(!state->done){WinHttpCloseHandle(request);delete state;return {};}
        DWORD_PTR context=reinterpret_cast<DWORD_PTR>(state);
        if(!WinHttpSetOption(request,WINHTTP_OPTION_CONTEXT_VALUE,&context,sizeof(context))){WinHttpCloseHandle(request);delete state;return {};}
        DWORD disable=WINHTTP_DISABLE_REDIRECTS;WinHttpSetOption(request,WINHTTP_OPTION_DISABLE_FEATURE,&disable,sizeof(disable));
        auto prior=WinHttpSetStatusCallback(request,callback,WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS|WINHTTP_CALLBACK_FLAG_HANDLES,0);
        if(prior==WINHTTP_INVALID_STATUS_CALLBACK){WinHttpCloseHandle(request);delete state;return {};}
        BOOL sent=WinHttpSendRequest(request,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,context);
        bool completed=false;
        if(sent || GetLastError()==ERROR_IO_PENDING)completed=WaitForSingleObject(state->done,200)==WAIT_OBJECT_0;
        state->closing=true;
        HttpResult result;
        {std::lock_guard guard(state->mutex);if(completed)result={state->available,state->status,std::move(state->body)};}
        WinHttpCloseHandle(request);
        // Windows owns one reference until HANDLE_CLOSING; the caller never waits for cleanup.
        state->release();return result;
    }
};
}
