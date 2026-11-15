#include <windows.h>
#include <cstdint>
#include <cstdlib>
#include <string>
static bool transfer(HANDLE h,void* data,DWORD bytes,bool write) {
    auto* p=static_cast<unsigned char*>(data);
    while(bytes){DWORD n{};if(!(write?WriteFile(h,p,bytes,&n,nullptr):ReadFile(h,p,bytes,&n,nullptr))||!n)return false;p+=n;bytes-=n;}return true;
}
int wmain(int argc,wchar_t** argv) {
    HANDLE input{},output{};
    for(int i=1;i+1<argc;i+=2){auto h=reinterpret_cast<HANDLE>(static_cast<uintptr_t>(_wcstoui64(argv[i+1],nullptr,10)));
        if(std::wstring(argv[i])==L"--pipe-read")input=h;else if(std::wstring(argv[i])==L"--pipe-write")output=h;}
    if(!input||!output)return 1;
    for(const std::string request:{"snapshot","scan_resources"}) {
        uint32_t n=static_cast<uint32_t>(request.size());
        if(!transfer(output,&n,4,true)||!transfer(output,const_cast<char*>(request.data()),n,true)||!transfer(input,&n,4,false)||n!=2)return 2;
        char response[2]{};if(!transfer(input,response,2,false)||std::string(response,2)!="ok")return 3;
    }
    return 0;
}
