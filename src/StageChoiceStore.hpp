#pragma once
#include "Catalog.hpp"
#include <filesystem>
#include <fstream>
#include <atomic>
#include <windows.h>
namespace GakuActors {
class StageChoiceStore {
    std::filesystem::path file_;bool writable_{};
public:
    void SetFile(const std::filesystem::path& file){file_=file;}
    bool Load(int& out) {
        writable_=false;if(file_.empty())return false;
        std::error_code ec;if(!std::filesystem::exists(file_,ec)){if(ec)return false;out=0;writable_=true;return true;}
        if(std::filesystem::file_size(file_,ec)>1024||ec)return false;
        std::ifstream in(file_,std::ios::binary);Json root=Json::parse(in,nullptr,false);
        if(!root.is_object()||!root.contains("version")||root["version"]!=1||!root.contains("choice")||
           !root["choice"].is_number_integer()||root["choice"]<0||root["choice"]>3)return false;
        const int choice=root["choice"].get<int>();
        out=choice;writable_=true;return true;
    }
    bool Save(int choice) {
        if(!writable_||choice<0||choice>3)return false;
        std::error_code ec;std::filesystem::create_directories(file_.parent_path(),ec);if(ec)return false;
        static std::atomic<unsigned long> serial{};auto temp=file_;temp+=L"."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(++serial)+L".tmp";
        const auto bytes=Json{{"version",1},{"choice",choice}}.dump(2)+"\n";
        HANDLE h=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(h==INVALID_HANDLE_VALUE)return false;DWORD written{};
        bool ok=WriteFile(h,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size();
        ok=FlushFileBuffers(h)&&ok;CloseHandle(h);
        if(ok)ok=MoveFileExW(temp.c_str(),file_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
        if(!ok)DeleteFileW(temp.c_str());return ok;
    }
};
}
