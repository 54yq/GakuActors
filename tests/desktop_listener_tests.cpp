#include "DesktopListener.hpp"
#include <iostream>
int main(){
    DesktopListener server;if(!server.Start([](const std::string& s){return s=="request"?std::string("response"):std::string();}))return 1;
    const auto name=L"\\\\.\\pipe\\GakuActors-v1-"+std::to_wstring(GetCurrentProcessId());HANDLE client=INVALID_HANDLE_VALUE;
    for(int n=0;n<100&&client==INVALID_HANDLE_VALUE;++n){client=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);if(client==INVALID_HANDLE_VALUE)Sleep(10);}
    if(client==INVALID_HANDLE_VALUE){std::cerr<<"client error "<<GetLastError()<<" server error "<<server.LastError()<<"\n";return 2;}
    DWORD actual{};uint32_t size=7;char buffer[8]{};
    if(!WriteFile(client,&size,4,&actual,nullptr)||actual!=4||!WriteFile(client,"request",size,&actual,nullptr)||actual!=size)return 3;
    if(!ReadFile(client,&size,4,&actual,nullptr)||actual!=4||size!=8||!ReadFile(client,buffer,size,&actual,nullptr)||actual!=size||std::string(buffer,8)!="response")return 4;
    CloseHandle(client);server.Stop();
    if(!server.Start([](const std::string&){return std::string("ok");}))return 5;server.Stop();
    std::cout<<"Local IPC round trip and idle shutdown passed\n";return 0;
}
