#pragma once
#include <windows.h>
#include <commdlg.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>

namespace GakuActors {
// Uses the same installed desktop executable for catalog/export. The child
// owns its save dialog and decoder, so file operations never block Unity.
class LegacyMusic {
    struct Song {std::string name;bool cached{};};
    std::vector<Song> songs_;
    std::wstring root_,exe_,python_,script_,pendingFile_;
    HANDLE process_{};bool scanning_{};
    std::string status_{"点击检索乐库，读取游戏歌曲。"};
    char search_[160]{};
    bool Init() {
        if(!root_.empty())return !python_.empty()&&std::filesystem::exists(exe_)&&std::filesystem::exists(script_);
        std::wstring path(32768,L'\0');DWORD n=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
        if(!n||n>=path.size())return false;path.resize(n);
        root_=std::filesystem::path(path).parent_path().wstring();
        exe_=(std::filesystem::path(root_)/L"GakuLoader"/L"plugins"/L"GakuActors.Desktop.exe").wstring();
        script_=(std::filesystem::path(root_)/L"GakuLoader"/L"plugins"/L"GakuActors.music.py").wstring();
        wchar_t user[32768]{};const DWORD count=GetEnvironmentVariableW(L"USERPROFILE",user,32768);
        if(count&&count<32768)python_=(std::filesystem::path(user)/L"anaconda3"/L"python.exe").wstring();
        if(python_.empty()||!std::filesystem::exists(python_)) {wchar_t found[32768]{};const DWORD n=SearchPathW(nullptr,L"python.exe",nullptr,32768,found,nullptr);if(n&&n<32768)python_=found;}
        return !python_.empty()&&std::filesystem::exists(exe_)&&std::filesystem::exists(script_);
    }
    bool Start(const std::wstring& program,const std::wstring& args,bool scan) {
        if(process_||!Init())return false;
        std::wstring command=L"\""+program+L"\" "+args;
        STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;
        PROCESS_INFORMATION pi{};
        if(!CreateProcessW(program.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,root_.c_str(),&si,&pi))return false;
        CloseHandle(pi.hThread);process_=pi.hProcess;scanning_=scan;return true;
    }
    void Scan() {
        if(process_||!Init()){status_="音频工具不可用或正在运行。";return;}
        wchar_t temp[MAX_PATH+1]{},file[MAX_PATH+1]{};
        if(!GetTempPathW(MAX_PATH,temp)||!GetTempFileNameW(temp,L"gkm",0,file)){status_="无法创建临时目录文件。";return;}
        pendingFile_=file;
        if(!Start(python_,L"\""+script_+L"\" --game \""+root_+L"\" --json \""+pendingFile_+L"\"",true)) {
            DeleteFileW(pendingFile_.c_str());pendingFile_.clear();status_="无法启动歌曲检索。";return;
        }
        status_="正在检索乐库…";
    }
    void Export(const std::string& name) {
        if(process_||!Init())return;
        if(name.empty()||name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos){status_="歌曲资源名无效。";return;}
        std::wstring wideName(name.begin(),name.end());
        std::wstring defaultName=wideName;if(defaultName.size()>=4&&defaultName.substr(defaultName.size()-4)==L".awb")defaultName.resize(defaultName.size()-4);defaultName+=L".wav";
        std::vector<wchar_t> output(32768);std::copy(defaultName.begin(),defaultName.end(),output.begin());
        OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.lpstrFilter=L"WAV 音频\0*.wav\0\0";dialog.nFilterIndex=1;dialog.lpstrFile=output.data();dialog.nMaxFile=static_cast<DWORD>(output.size());dialog.lpstrDefExt=L"wav";dialog.Flags=OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
        if(!GetSaveFileNameW(&dialog))return;
        const std::wstring outputPath(output.data());
        if(std::filesystem::exists(outputPath)){status_="目标文件已存在，请选择其他名称。";return;}
        if(Start(python_,L"\""+script_+L"\" --game \""+root_+L"\" --filter \""+wideName+L"\" --export \""+wideName+L"\" --output \""+outputPath+L"\"",false))status_="正在后台导出歌曲…";
        else status_="无法启动歌曲导出。";
    }
    void Poll() {
        if(!process_)return;DWORD exit{};if(!GetExitCodeProcess(process_,&exit)||exit==STILL_ACTIVE)return;
        CloseHandle(process_);process_=nullptr;
        if(scanning_) {
            std::ifstream in(std::filesystem::path(pendingFile_),std::ios::binary|std::ios::ate);
            try {
                if(exit!=0||!in||in.tellg()<0||in.tellg()>8*1024*1024)throw std::runtime_error("catalog unavailable");
                in.seekg(0);nlohmann::json data;in>>data;if(data.is_object()&&data.contains("songs")){auto list=data.at("songs");data=std::move(list);}if(!data.is_array())throw std::runtime_error("invalid catalog");
                std::vector<Song> next;size_t cached=0;
                for(const auto& item:data){if(!item.is_object())continue;auto name=item.value("name",std::string{});bool ready=item.value("cached",false);if(!name.empty()){next.push_back({std::move(name),ready});if(ready)++cached;}}
                songs_=std::move(next);status_="检索完成："+std::to_string(songs_.size())+" 个版本，"+std::to_string(cached)+" 个已缓存。";
            } catch(...) {status_="歌曲检索失败，请检查 Python 和游戏资源清单。";}
            in.close();DeleteFileW(pendingFile_.c_str());pendingFile_.clear();
        } else status_=exit==0?"歌曲导出流程已结束。":"歌曲导出失败。";
    }
public:
    ~LegacyMusic(){if(process_)CloseHandle(process_);if(!pendingFile_.empty())DeleteFileW(pendingFile_.c_str());}
    void Draw() {
        Poll();ImGui::BeginDisabled(process_!=nullptr);
        if(ImGui::Button("检索乐库"))Scan();ImGui::EndDisabled();ImGui::SameLine();ImGui::TextWrapped("%s",status_.c_str());
        ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##music_search","搜索歌曲资源名",search_,sizeof(search_));
        ImGui::TextDisabled("Live 与通用版本；资源名为游戏内部标识。");
        if(ImGui::BeginChild("##legacy_music_list",ImVec2(0,390),true)) {
            for(size_t i=0;i<songs_.size();++i){const auto& song=songs_[i];if(search_[0]&&song.name.find(search_)==std::string::npos)continue;
                ImGui::PushID(static_cast<int>(i));ImGui::BeginDisabled(!song.cached||process_!=nullptr);
                if(ImGui::Button("导出"))Export(song.name);ImGui::EndDisabled();ImGui::SameLine();
                ImGui::TextWrapped("%s  %s",song.cached?"[已缓存]":"[未下载]",song.name.c_str());ImGui::PopID();
            }
        }ImGui::EndChild();
    }
};
}
