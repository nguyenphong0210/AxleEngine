#include <Axle/FontRasterizer.hpp>
#include <cstring>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace Axle {
    std::shared_ptr<const RasterizedFontAtlas> GetDefaultFontAtlas() {
        static const auto cached = []() -> std::shared_ptr<const RasterizedFontAtlas> {
            auto atlas = std::make_shared<RasterizedFontAtlas>();
#if defined(_WIN32)
        // Rasterize the platform's installed monospace font into an owned CPU
        // coverage atlas. No UI library/context or GPU is needed for decoding.
        constexpr int cell = 32, columns = 16, width = cell * columns, height = cell * 6;
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        auto dc = CreateCompatibleDC(nullptr); void* bits = nullptr;
        auto bitmap = CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        auto font = CreateFontW(-24,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,FF_MODERN,L"Consolas");
        if (dc && bitmap && font && bits) {
            auto oldBitmap = SelectObject(dc,bitmap); auto oldFont = SelectObject(dc,font);
            std::memset(bits,0,width*height*4); SetTextColor(dc,RGB(255,255,255)); SetBkColor(dc,RGB(0,0,0));
            for (unsigned cp=33; cp<=126; ++cp) {
                const int index = static_cast<int>(cp)-32, x=(index%columns)*cell, y=(index/columns)*cell;
                wchar_t text = static_cast<wchar_t>(cp); TextOutW(dc,x,y,&text,1);
                atlas->GlyphUV[index] = {float(x)/width,float(y)/height,float(x+cell)/width,float(y+cell)/height};
            }
            GdiFlush();
            auto image = std::make_shared<DecodedImage>();
            image->Mips.push_back({width,height,std::vector<uint8_t>(width*height*4)});
            auto* source = static_cast<uint8_t*>(bits);
            for (size_t i=0;i<image->Mips[0].Pixels.size();i+=4) {
                image->Mips[0].Pixels[i]=image->Mips[0].Pixels[i+1]=image->Mips[0].Pixels[i+2]=255;
                image->Mips[0].Pixels[i+3]=source[i];
            }
            atlas->Source.SourceKey=atlas->Source.RequestKey="builtin-font:Consolas:24:coverage";
            atlas->Source.Revision=ImageContentRevision(image->Mips[0].Pixels); atlas->Source.Image=std::move(image);
            SelectObject(dc,oldFont); SelectObject(dc,oldBitmap);
        }
        if (font) DeleteObject(font); if (bitmap) DeleteObject(bitmap); if (dc) DeleteDC(dc);
#endif


            return atlas->Source.Image ? atlas : nullptr;
        }();
        return cached;
    }
}
