#include "DesktopTransport.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 1;
    GakuActors::DesktopTransport transport;std::atomic<int> count{};std::string error;
    if(!transport.Start(argv[1],[&](const std::string& input){
        if(input.find("snapshot")!=std::string::npos||input.find("scan_resources")!=std::string::npos)++count;
        return std::string("{\"version\":1,\"accepted\":true,\"snapshot\":{}}");
    },error,true)){std::cerr<<error;return 2;}
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(transport.Running()&&std::chrono::steady_clock::now()<deadline)Sleep(10);
    DWORD result{};const bool passed=!transport.Running()&&transport.ExitCode(result)&&result==0&&count==2;
    transport.Stop();if(!passed)return 3;
    std::cout<<"Rust/C++ inherited-handle JSON round trip passed\n";return 0;
}
