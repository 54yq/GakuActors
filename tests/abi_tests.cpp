#include <GakuPlugin.h>
#include <windows.h>
#include <cassert>
#include <iostream>
#include <cstring>

int wmain(int argc,wchar_t** argv) {
    assert(argc==2);
    HMODULE dll=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    assert(dll);const auto query=reinterpret_cast<FnGakuPlugin_Query>(GetProcAddress(dll,"GakuPlugin_Query"));assert(query);
    GakuPluginDescriptor d{};std::memset(&d,0x5a,sizeof(d));const auto before=d;
    assert(query(999,sizeof(d),&d)==GAKU_E_VERSION&&std::memcmp(&d,&before,sizeof(d))==0);
    assert(query(1,1,&d)==GAKU_E_SIZE&&std::memcmp(&d,&before,sizeof(d))==0);
    assert(query(1,sizeof(d),nullptr)==GAKU_E_INVALID_ARGUMENT);
    assert(query(1,sizeof(d),&d)==GAKU_OK);
    assert(std::strcmp(d.id,"gakuactors")==0&&d.activate&&d.onTick&&d.onDesktopUi&&d.shutdown);
    assert(std::strcmp(d.version,"0.5.7")==0);
    assert(d.onActorTick!=nullptr); // read-only scene admission, no private detour
    void* state=nullptr;assert(d.activate(nullptr,&state)==GAKU_E_INVALID_ARGUMENT&&state==nullptr);
    FreeLibrary(dll);std::cout<<"Plugin ABI query and independent lifecycle checks passed\n";
}
