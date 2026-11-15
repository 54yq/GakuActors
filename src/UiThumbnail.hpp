#pragma once
#include <windows.h>
#include <wincodec.h>
#include <wincrypt.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <string>
#include <imgui.h>

// Small CPU-decoded thumbnails avoid relying on the loader's private graphics
// device. Inputs are bounded PNGs returned by the game's own texture encoder.
namespace GakuActors::ui_thumbnail {
struct Image {static constexpr int size=48;std::array<unsigned char,size*size*4> rgba{};bool valid{};};
inline Image Decode(const std::string& encoded) {
    Image result;if(encoded.empty()||encoded.size()>700000)return result;
    DWORD length=0;if(!CryptStringToBinaryA(encoded.c_str(),static_cast<DWORD>(encoded.size()),CRYPT_STRING_BASE64,nullptr,&length,nullptr,nullptr)||length<8||length>512*1024)return result;
    std::vector<BYTE> bytes(length);if(!CryptStringToBinaryA(encoded.c_str(),static_cast<DWORD>(encoded.size()),CRYPT_STRING_BASE64,bytes.data(),&length,nullptr,nullptr))return result;
    const HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);const bool uninit=SUCCEEDED(init);
    if(FAILED(init)&&init!=RPC_E_CHANGED_MODE)return result;
    {
        using Microsoft::WRL::ComPtr;ComPtr<IWICImagingFactory> factory;ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICBitmapScaler> scaler;ComPtr<IWICFormatConverter> converter;
        if(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))&&
           SUCCEEDED(factory->CreateStream(&stream))&&SUCCEEDED(stream->InitializeFromMemory(bytes.data(),length))&&
           SUCCEEDED(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder))&&
           SUCCEEDED(decoder->GetFrame(0,&frame))) {
            UINT width=0,height=0;
            if(SUCCEEDED(frame->GetSize(&width,&height))&&width&&height&&width<=1024&&height<=1024&&
               SUCCEEDED(factory->CreateBitmapScaler(&scaler))&&SUCCEEDED(scaler->Initialize(frame.Get(),Image::size,Image::size,WICBitmapInterpolationModeFant))&&
               SUCCEEDED(factory->CreateFormatConverter(&converter))&&SUCCEEDED(converter->Initialize(scaler.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))&&
               SUCCEEDED(converter->CopyPixels(nullptr,Image::size*4,static_cast<UINT>(result.rgba.size()),result.rgba.data())))result.valid=true;
        }
    }
    if(uninit)CoUninitialize();return result;
}
inline void Draw(const Image& image) {
    constexpr float edge=96.0f;const ImVec2 p=ImGui::GetCursorScreenPos();ImGui::Dummy(ImVec2(edge,edge));
    auto* draw=ImGui::GetWindowDrawList();draw->AddRectFilled(p,ImVec2(p.x+edge,p.y+edge),IM_COL32(235,238,244,255));
    if(!image.valid)return;
    constexpr float step=edge/Image::size;
    for(int y=0;y<Image::size;++y)for(int x=0;x<Image::size;++x) {
        const auto i=(y*Image::size+x)*4;const auto* c=image.rgba.data()+i;
        if(c[3])draw->AddRectFilled(ImVec2(p.x+x*step,p.y+y*step),ImVec2(p.x+(x+1)*step,p.y+(y+1)*step),IM_COL32(c[0],c[1],c[2],c[3]));
    }
}
}
