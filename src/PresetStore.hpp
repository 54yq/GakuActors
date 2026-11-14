#pragma once
#include "Catalog.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <windows.h>

namespace GakuActors {
class PresetStore {
    std::filesystem::path file_;
    bool writable_{false};
public:
    std::vector<Preset> presets;
    std::string notice;
    void SetFile(std::filesystem::path file){file_=std::move(file);}
    bool Writable()const{return writable_;}
    bool Load() {
        writable_=false;
        std::error_code ec;
        if(file_.empty()){notice="预设目录不可用";return false;}
        if(!std::filesystem::exists(file_,ec)) {
            if(ec){notice="无法检查预设文件";return false;}
            presets.clear();writable_=true;notice.clear();return true;
        }
        if(std::filesystem::file_size(file_,ec)>1024*1024||ec){notice="预设文件过大或不可读，已保留原文件";return false;}
        std::ifstream input(file_,std::ios::binary);
        Json root=Json::parse(input,nullptr,false);
        if(root.is_discarded()||!root.is_object()||!root.contains("version")||root["version"]!=1||
           !root.contains("presets")||!root["presets"].is_array()||root["presets"].size()>256) {
            notice="预设文件无效，已保留原文件";return false;
        }
        std::vector<Preset> next;
        for(const auto& j:root["presets"]) {
            Preset p;std::string error;
            if(!ReadPreset(j,p,error)||std::any_of(next.begin(),next.end(),[&](const auto& x){return x.id==p.id;})) {
                notice="预设字段或标识无效，已保留原文件";return false;
            }
            next.push_back(std::move(p));
        }
        presets=std::move(next);writable_=true;notice.clear();return true;
    }
    bool Save(Preset preset) {
        std::string error;
        if(!writable_){notice="预设文件不可写，请先修复文件并重新读取";return false;}
        if(!ValidatePreset(preset,error)){notice=error;return false;}
        auto next=presets;
        auto found=std::find_if(next.begin(),next.end(),[&](const auto& p){return p.id==preset.id;});
        preset.builtin=false;
        if(found==next.end()) {
            if(next.size()>=256){notice="最多保存 256 个预设";return false;}
            next.push_back(std::move(preset));
        } else *found=std::move(preset);
        if(!Write(next))return false;
        presets=std::move(next);notice="人物预设已保存";return true;
    }
    bool Remove(const std::string& id) {
        if(!writable_){notice="预设文件不可写";return false;}
        auto next=presets;
        next.erase(std::remove_if(next.begin(),next.end(),[&](const auto& p){return p.id==id;}),next.end());
        if(next.size()==presets.size())return false;
        if(!Write(next))return false;
        presets=std::move(next);notice="预设已删除，场景人物保持不变";return true;
    }
private:
    bool Write(const std::vector<Preset>& values) {
        Json root{{"version",1},{"presets",Json::array()}};
        for(const auto& p:values)root["presets"].push_back(PresetJson(p));
        const std::string bytes=root.dump(2,' ',false,Json::error_handler_t::strict)+"\n";
        std::error_code ec;std::filesystem::create_directories(file_.parent_path(),ec);
        if(ec){notice="无法创建预设目录";return false;}
        static std::atomic<uint64_t> serial{};
        auto temp=file_;temp+=L"."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(++serial)+L".tmp";
        HANDLE handle=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle==INVALID_HANDLE_VALUE){notice="无法写入预设临时文件";return false;}
        DWORD written{};bool ok=WriteFile(handle,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size();
        ok=FlushFileBuffers(handle)&&ok;CloseHandle(handle);
        if(ok)ok=MoveFileExW(temp.c_str(),file_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
        if(!ok){DeleteFileW(temp.c_str());notice="预设保存失败，原文件未覆盖";return false;}
        return true;
    }
};
}
