// Reads only a thumbnail already resident in Unity. No resource download,
// bundle load, model creation or render-target manipulation is performed.
static std::string ThumbnailBase64(const unsigned char* data,size_t size) {
    static constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;result.reserve(((size+2)/3)*4);
    for(size_t i=0;i<size;i+=3) {
        const uint32_t n=(static_cast<uint32_t>(data[i])<<16)|
            (i+1<size?static_cast<uint32_t>(data[i+1])<<8:0)|
            (i+2<size?static_cast<uint32_t>(data[i+2]):0);
        result.push_back(alphabet[(n>>18)&63]);result.push_back(alphabet[(n>>12)&63]);
        result.push_back(i+1<size?alphabet[(n>>6)&63]:'=');result.push_back(i+2<size?alphabet[n&63]:'=');
    }
    return result;
}
std::string ReadLoadedThumbnail(const std::string& name,std::string& status) {
    status="缩略图尚不可用";if(!PerformanceScene()||!TextValid(name,256))return {};
    try {
        void* klass=Class("UnityEngine","Texture2D"),*image=Class("UnityEngine","ImageConversion");
        if(!klass||!image){status="游戏缩略图接口不可用";return {};}
        void* found=nullptr;
        for(void* texture:Objects(klass))if(Text(texture,"get_name")==name){if(found){status="缩略图名称不唯一";return {};}found=texture;}
        if(!found){status="官方图片尚未加载；可在游戏中打开对应服装页面后重试";return {};}
        const auto encode=Method(image,"EncodeToPNG",1,true,{0x12});
        if(!encode||typeCode_(result_(encode))!=0x1d||!assignable_(fromType_(param_(encode,0)),objectClass_(found))){status="游戏图片编码接口不可用";return {};}
        void* args[]{found};Pin bytes(*this,Call(encode,nullptr,args));
        if(!bytes.Get()||ClassName(objectClass_(bytes.Get()))!="System.Byte[]"){status="官方图片编码未返回数据";return {};}
        const auto n=arrayLength_(bytes.Get());if(n<8||n>512*1024){status="缩略图大小不受支持";return {};}
        const auto* raw=reinterpret_cast<const unsigned char*>(static_cast<const char*>(bytes.Get())+4*sizeof(void*));
        constexpr unsigned char signature[]{137,80,78,71,13,10,26,10};
        if(std::memcmp(raw,signature,sizeof(signature))){status="缩略图格式不受支持";return {};}
        status="游戏目录缩略图";return ThumbnailBase64(raw,static_cast<size_t>(n));
    }catch(const std::exception&) {status="官方图片不可读或尚未就绪";return {};}
}
