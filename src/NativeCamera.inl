// Camera operations run only on the admitted Unity-thread scene callback.
struct CameraMethods {const void *getFlags{},*setFlags{},*getColor{},*setColor{};};
CameraMethods CameraApi(void* camera) {
    CameraMethods m{On(camera,"get_clearFlags"),Method(objectClass_(camera),"set_clearFlags",1,false,{0x11}),
        On(camera,"get_backgroundColor"),Method(objectClass_(camera),"set_backgroundColor",1,false,{0x11})};
    auto flags=Class("UnityEngine","CameraClearFlags"),color=Class("UnityEngine","Color");uint32_t align{};
    if(!m.getFlags||!m.setFlags||!m.getColor||!m.setColor||!flags||!color||
       fromType_(result_(m.getFlags))!=flags||fromType_(param_(m.setFlags,0))!=flags||
       fromType_(result_(m.getColor))!=color||fromType_(param_(m.setColor,0))!=color||
       typeCode_(result_(m.setFlags))!=1||typeCode_(result_(m.setColor))!=1||
       valueSize_(flags,&align)!=4||valueSize_(color,&align)!=16)
        throw std::runtime_error("相机颜色接口不匹配");
    return m;
}
void ReadCamera(void* camera,const CameraMethods& m,int& flags,float* color) {
    Pin flagValue(*this,Call(m.getFlags,camera));Pin colorValue(*this,Call(m.getColor,camera));
    if(!flagValue.Get()||!colorValue.Get())throw std::runtime_error("相机状态不可读");
    std::memcpy(&flags,unbox_(flagValue.Get()),sizeof(flags));std::memcpy(color,unbox_(colorValue.Get()),4*sizeof(float));
    for(int i=0;i<4;++i)if(!std::isfinite(color[i]))throw std::runtime_error("相机颜色无效");
}
bool CameraSame(const float* a,const float* b) {
    for(int i=0;i<4;++i)if(!std::isfinite(a[i])||std::fabs(a[i]-b[i])>1e-5f)return false;
    return true;
}
bool RestoreCamera() {
    auto& lease=cameraLease_;if(!lease.handle)return true;
    try {
        void* camera=handleTarget_(lease.handle);
        if(!camera||Int(camera,"GetInstanceID")!=lease.instance||SceneOf(GameObject(camera))!=lease.scene)return false;
        const auto m=CameraApi(camera);int flags{};float color[4]{};ReadCamera(camera,m,flags,color);
        if(lease.flagsWritten&&flags!=lease.originalFlags) {
            if(flags!=lease.writtenFlags)return false;
            void* args[]{&lease.originalFlags};Call(m.setFlags,camera,args);
        }
        if(lease.colorWritten&&!CameraSame(color,lease.originalColor)) {
            if(!CameraSame(color,lease.writtenColor))return false;
            void* args[]{lease.originalColor};Call(m.setColor,camera,args);
        }
        ReadCamera(camera,m,flags,color);
        if((lease.flagsWritten&&flags!=lease.originalFlags)||(lease.colorWritten&&!CameraSame(color,lease.originalColor)))return false;
        freeHandle_(lease.handle);lease={};return true;
    } catch(...) {return false;}
}
bool ApplyCameraColor(const float* color) {
    if(!PerformanceScene()||!color)return false;
    for(int i=0;i<4;++i)if(!std::isfinite(color[i])||color[i]<0||color[i]>1)return false;
    void* klass=Class("UnityEngine","Camera");const auto main=Method(klass,"get_main",0,true);
    Pin camera(*this,Call(main));if(!camera.Get())return false;
    const int id=Int(camera.Get(),"GetInstanceID"),scene=SceneOf(GameObject(camera.Get()));
    if(!id||!scene||(scene!=PerformanceScene()&&scene!=ActiveSceneHandle()))return false;
    auto& lease=cameraLease_;
    if(lease.handle&&(handleTarget_(lease.handle)!=camera.Get()||lease.instance!=id||lease.scene!=scene))return false;
    const auto m=CameraApi(camera.Get());int currentFlags{};float currentColor[4]{};ReadCamera(camera.Get(),m,currentFlags,currentColor);
    if(!lease.handle) {
        lease.handle=newHandle_(camera.Get(),false);if(!lease.handle)return false;
        lease.instance=id;lease.scene=scene;lease.originalFlags=currentFlags;
        std::memcpy(lease.originalColor,currentColor,sizeof(lease.originalColor));
    } else if((lease.flagsWritten&&currentFlags!=lease.writtenFlags)||
              (lease.colorWritten&&!CameraSame(currentColor,lease.writtenColor)))return false;
    // Record intent before each setter: a failing call may already have written.
    lease.writtenFlags=2;lease.flagsWritten=true;
    void* flagArgs[]{&lease.writtenFlags};Call(m.setFlags,camera.Get(),flagArgs);
    std::memcpy(lease.writtenColor,color,sizeof(lease.writtenColor));lease.colorWritten=true;
    void* colorArgs[]{lease.writtenColor};Call(m.setColor,camera.Get(),colorArgs);
    ReadCamera(camera.Get(),m,currentFlags,currentColor);
    return currentFlags==lease.writtenFlags&&CameraSame(currentColor,lease.writtenColor);
}
