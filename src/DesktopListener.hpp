#pragma once
#include <windows.h>
#include <sddl.h>
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

// Local-only, current-user IPC for independently launched desktop windows.
class DesktopListener {
    std::atomic<bool> stopping_{false}; std::atomic<DWORD> error_{0}; std::thread worker_;
    static bool ReadExact(HANDLE h,void* p,DWORD n) {auto b=static_cast<char*>(p);while(n){DWORD done=0;if(!ReadFile(h,b,n,&done,nullptr)||!done)return false;b+=done;n-=done;}return true;}
    static bool WriteExact(HANDLE h,const void* p,DWORD n) {auto b=static_cast<const char*>(p);while(n){DWORD done=0;if(!WriteFile(h,b,n,&done,nullptr)||!done)return false;b+=done;n-=done;}return true;}
    static PSECURITY_DESCRIPTOR UserDescriptor() {
        HANDLE token{};if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return nullptr;
        DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);std::vector<unsigned char> data(size);
        const bool ok=size&&GetTokenInformation(token,TokenUser,data.data(),size,&size);CloseHandle(token);if(!ok)return nullptr;
        LPWSTR sid{};if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,&sid))return nullptr;
        std::wstring sddl=L"D:P(A;;GA;;;SY)(A;;GA;;;";sddl+=sid;sddl+=L")";LocalFree(sid);
        PSECURITY_DESCRIPTOR descriptor{};if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr))return nullptr;return descriptor;
    }
public:
    ~DesktopListener(){Stop();}
    bool Start(std::function<std::string(const std::string&)> handler,std::function<void()> disconnected={}) {
        if(worker_.joinable())return true;
        auto descriptor=UserDescriptor();if(!descriptor)return false;
        stopping_=false;
        try {worker_=std::thread([this,handler=std::move(handler),disconnected=std::move(disconnected),descriptor]{
            const std::wstring name=L"\\\\.\\pipe\\GakuActors-v1-"+std::to_wstring(GetCurrentProcessId());
            SECURITY_ATTRIBUTES sa{sizeof(sa),descriptor,FALSE};
            while(!stopping_) {
                HANDLE pipe=CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,0,&sa);
                if(pipe==INVALID_HANDLE_VALUE){error_=GetLastError();break;}
                const bool connected=ConnectNamedPipe(pipe,nullptr)||GetLastError()==ERROR_PIPE_CONNECTED;
                if(connected)while(!stopping_) {
                    uint32_t n{};if(!ReadExact(pipe,&n,4)||!n||n>64*1024)break;
                    std::string in(n,'\0');if(!ReadExact(pipe,in.data(),n))break;
                    std::string out;try{out=handler(in);}catch(...){break;}
                    if(out.empty()||out.size()>16*1024*1024)break;
                    n=static_cast<uint32_t>(out.size());if(!WriteExact(pipe,&n,4)||!WriteExact(pipe,out.data(),n))break;
                }
                if(connected&&disconnected)try{disconnected();}catch(...){}
                DisconnectNamedPipe(pipe);CloseHandle(pipe);
            }LocalFree(descriptor);
        });}catch(...){LocalFree(descriptor);return false;}return true;
    }
    DWORD LastError()const{return error_.load();}
    void Stop() {stopping_=true;if(worker_.joinable()){while(WaitForSingleObject(worker_.native_handle(),25)==WAIT_TIMEOUT)CancelSynchronousIo(worker_.native_handle());worker_.join();}}
};
