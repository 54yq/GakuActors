#pragma once
#include <windows.h>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>
#include <atomic>

namespace GakuActors {
// On-demand child process transport: only the two anonymous pipe endpoints are
// inherited. No listening port, pipe name, or Unity pointer is exposed.
class DesktopTransport {
    HANDLE read_{},write_{},process_{};
    std::thread worker_;
    std::atomic<bool> stopping_{};
    static constexpr uint32_t kMaxRequest=64*1024,kMaxReply=16*1024*1024;
    static void Close(HANDLE& h){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);h=nullptr;}
    static bool ReadExact(HANDLE h,void* buffer,DWORD size) {
        auto* p=static_cast<unsigned char*>(buffer);
        while(size){DWORD n{};if(!ReadFile(h,p,size,&n,nullptr)||!n)return false;p+=n;size-=n;}return true;
    }
    static bool WriteExact(HANDLE h,const void* buffer,DWORD size) {
        auto* p=static_cast<const unsigned char*>(buffer);
        while(size){DWORD n{};if(!WriteFile(h,p,size,&n,nullptr)||!n)return false;p+=n;size-=n;}return true;
    }
public:
    using Handler=std::function<std::string(const std::string&)>;
    DesktopTransport()=default;
    DesktopTransport(const DesktopTransport&)=delete;
    DesktopTransport& operator=(const DesktopTransport&)=delete;
    ~DesktopTransport(){Stop();}
    bool Running()const {return process_&&WaitForSingleObject(process_,0)==WAIT_TIMEOUT;}
    bool ExitCode(DWORD& code)const {return process_&&GetExitCodeProcess(process_,&code)&&code!=STILL_ACTIVE;}
    void Stop() {
        stopping_.store(true,std::memory_order_release);
        if(worker_.joinable()) {
            // Repeating cancellation closes the race in which Stop arrives just
            // before the worker enters a synchronous ReadFile or WriteFile.
            while(WaitForSingleObject(worker_.native_handle(),25)==WAIT_TIMEOUT)CancelSynchronousIo(worker_.native_handle());
            worker_.join();
        }
        Close(read_);Close(write_);Close(process_);
    }
    bool Start(const std::wstring& exe,Handler handler,std::string& error,bool selfTest=false) {
        error.clear();if(Running()){error="独立窗口已经打开";return false;}Stop();
        SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE childRead{},childWrite{};
        if(!CreatePipe(&childRead,&write_,&sa,0)||!CreatePipe(&read_,&childWrite,&sa,0)||
           !SetHandleInformation(read_,HANDLE_FLAG_INHERIT,0)||!SetHandleInformation(write_,HANDLE_FLAG_INHERIT,0)) {
            error="无法创建窗口通信管道";Close(childRead);Close(childWrite);Stop();return false;
        }
        SIZE_T bytes{};InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
        std::vector<unsigned char> storage(bytes);auto* attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if(!bytes||!InitializeProcThreadAttributeList(attributes,1,0,&bytes)) {
            error="无法初始化进程属性";Close(childRead);Close(childWrite);Stop();return false;
        }
        HANDLE inherited[]{childRead,childWrite};
        bool valid=UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr)!=0;
        STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=attributes;
        PROCESS_INFORMATION pi{};
        std::wstring cmd=L"\""+exe+L"\" --pipe-read "+std::to_wstring(reinterpret_cast<uintptr_t>(childRead))+L" --pipe-write "+std::to_wstring(reinterpret_cast<uintptr_t>(childWrite));
        if(selfTest)cmd+=L" --ipc-self-test";
        if(valid)valid=CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,TRUE,EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&startup.StartupInfo,&pi)!=0;
        const DWORD lastError=GetLastError();DeleteProcThreadAttributeList(attributes);Close(childRead);Close(childWrite);
        if(!valid){error="无法启动独立窗口，Windows 错误="+std::to_string(lastError);Stop();return false;}
        CloseHandle(pi.hThread);process_=pi.hProcess;
        stopping_.store(false,std::memory_order_release);
        try {
            worker_=std::thread([this,handler=std::move(handler)]{
                try {
                    while(!stopping_.load(std::memory_order_acquire)){uint32_t length{};if(!ReadExact(read_,&length,sizeof(length))||!length||length>kMaxRequest)break;
                        std::string input(length,'\0');if(!ReadExact(read_,input.data(),length))break;
                        std::string output=handler(input);if(output.empty()||output.size()>kMaxReply)break;
                        if(stopping_.load(std::memory_order_acquire))break;
                        const auto n=static_cast<uint32_t>(output.size());if(!WriteExact(write_,&n,sizeof(n))||!WriteExact(write_,output.data(),n))break;
                    }
                }catch(...){}
            });
        }catch(...){error="无法启动窗口通信线程";Stop();return false;}
        return true;
    }
};
}
