#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include "HostedHost.hpp"

namespace GkmsHosted {
// Only metadata for the fixed set of legacy Unity input methods is resolved.
class FreeCameraInputApi {
    HMODULE module_{};
    void*(*domain_)(){};const void**(*assemblies_)(void*,size_t*){};void*(*image_)(const void*){};
    void*(*findClass_)(void*,const char*,const char*){};void*(*parent_)(void*){};
    const void*(*methods_)(void*,void**){};const char*(*name_)(const void*){};uint32_t(*count_)(const void*){};
    uint32_t(*flags_)(const void*,uint32_t*){};void*(*param_)(const void*,uint32_t){};void*(*result_)(const void*){};
    int(*type_)(void*){};bool(*byref_)(void*){};void*(*fromType_)(void*){};bool(*isEnum_)(void*){};
    int(*length_)(void*){};const wchar_t*(*chars_)(void*){};
    template<class T> bool Export(T& fn,const char* n){fn=reinterpret_cast<T>(GetProcAddress(module_,n));return fn!=nullptr;}
    void* Class(const char* ns,const char* name){size_t count{};auto items=assemblies_(domain_(),&count);if(!items||count>8192)return nullptr;for(size_t i=0;i<count;++i)if(auto klass=findClass_(image_(items[i]),ns,name))return klass;return nullptr;}
    const void* Method(void* klass,const char* name,int paramCode){
        for(void* current=klass;current;current=parent_(current)){
            const void* found=nullptr;void* it=nullptr;
            while(const void* m=methods_(current,&it)){
                uint32_t impl{};if(std::strcmp(name_(m),name)||count_(m)!=1||!(flags_(m,&impl)&0x10))continue;
                void* p=param_(m,0);if(!p||byref_(p)||type_(p)!=paramCode)continue;
                if(found)return nullptr;found=m;
            }
            if(found)return found;
        }
        return nullptr;
    }
public:
    struct FreeInputContract{void* targets[14]{};};
    void Trace(const char* text){HostLog(GAKU_LOG_INFO,text?text:"");}
    bool Resolve(void* module){
        module_=static_cast<HMODULE>(module);if(!module_)return false;
        return Export(domain_,"il2cpp_domain_get")&&Export(assemblies_,"il2cpp_domain_get_assemblies")&&Export(image_,"il2cpp_assembly_get_image")&&
          Export(findClass_,"il2cpp_class_from_name")&&Export(parent_,"il2cpp_class_get_parent")&&Export(methods_,"il2cpp_class_get_methods")&&
          Export(name_,"il2cpp_method_get_name")&&Export(count_,"il2cpp_method_get_param_count")&&Export(flags_,"il2cpp_method_get_flags")&&
          Export(param_,"il2cpp_method_get_param")&&Export(result_,"il2cpp_method_get_return_type")&&Export(type_,"il2cpp_type_get_type")&&
          Export(byref_,"il2cpp_type_is_byref")&&Export(fromType_,"il2cpp_class_from_type")&&Export(isEnum_,"il2cpp_class_is_enum")&&
          Export(length_,"il2cpp_string_length")&&Export(chars_,"il2cpp_string_chars");
    }
    bool ResolveFreeInputContract(FreeInputContract& out){
        out={};void* input=Class("UnityEngine","Input"),*key=Class("UnityEngine","KeyCode");if(!input||!key||!isEnum_(key))return false;
        const char* keys[]{"GetKey","GetKeyDown","GetKeyUp"};
        for(int i=0;i<3;++i){const auto a=Method(input,keys[i],0x11),b=Method(input,keys[i],0x0e);if(!a||!b||fromType_(param_(a,0))!=key||type_(result_(a))!=2||type_(result_(b))!=2)return false;out.targets[i]=*static_cast<void* const*>(a);out.targets[i+3]=*static_cast<void* const*>(b);}
        const char* names[]{"GetAxis","GetAxisRaw","GetButton","GetButtonDown","GetButtonUp"};
        for(int i=0;i<5;++i){const auto m=Method(input,names[i],0x0e);if(!m||type_(result_(m))!=(i<2?0x0c:2))return false;out.targets[i+6]=*static_cast<void* const*>(m);}
        const char* internal[]{"GetKeyInt","GetKeyDownInt","GetKeyUpInt"};
        for(int i=0;i<3;++i){const auto m=Method(input,internal[i],0x11);if(!m||fromType_(param_(m,0))!=key||type_(result_(m))!=2)return false;out.targets[i+11]=*static_cast<void* const*>(m);}
        const auto kind=[](int i){return i<3||i>=11?0:i<6?1:i<8?2:3;};
        for(int i=0;i<14;++i){if(!out.targets[i])return false;for(int j=0;j<i;++j)if(out.targets[i]==out.targets[j]&&kind(i)!=kind(j))return false;}
        return true;
    }
    bool FreeInputStringEquals(void* value,const wchar_t* expected)const noexcept{
        if(!value||!expected||!length_||!chars_)return false;const int n=length_(value);if(n<=0||n>32)return false;
        const auto* chars=chars_(value);if(!chars)return false;int i=0;for(;i<n&&expected[i];++i){wchar_t c=chars[i];if(c>=L'A'&&c<=L'Z')c+=L'a'-L'A';if(c!=expected[i])return false;}return i==n&&expected[i]==0;
    }
};
}
