#include "vertical_reading.h"
#include "d2d_init.h"
#include "search.h"
#include "tabs.h"
#include "input.h"
#include "overlays.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <memory>

// Input handlers in the linked app objects reference the application's painter.
void render(App&) {}

bool snapshot(App& app, const std::wstring& path) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICBitmap> bitmap;
    ComPtr<ID2D1RenderTarget> rt;
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(app.wicFactory->CreateBitmap(app.width, app.height,
        GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap)) ||
        FAILED(app.d2dFactory->CreateWicBitmapRenderTarget(bitmap.Get(),
        D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE), &rt)) ||
        FAILED(rt->CreateSolidColorBrush(app.theme.text, &brush))) return false;
    DWRITE_TEXT_METRICS m{};
    app.verticalLayout->GetMetrics(&m);
    rt->BeginDraw(); rt->Clear(app.theme.background);
    rt->PushAxisAlignedClip(D2D1::RectF(32,60,(float)app.width-32,(float)app.height-60),
                            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    rt->DrawTextLayout(D2D1::Point2F(app.width-32-m.left-m.width+app.verticalOffset,60),
                       app.verticalLayout.Get(), brush.Get());
    rt->PopAxisAlignedClip();
    if (FAILED(rt->EndDraw())) return false;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(app.wicFactory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
        FAILED(app.wicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->SetSize(app.width, app.height))) return false;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    return SUCCEEDED(frame->SetPixelFormat(&format)) && SUCCEEDED(frame->WriteSource(bitmap.Get(), nullptr)) &&
        SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}
int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc != 3) return 2;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    auto state = std::make_unique<App>(); auto& app = *state;
    app.hwnd = CreateWindowExW(0,L"STATIC",L"Vertical tests",WS_POPUP,0,0,1024,768,nullptr,nullptr,nullptr,nullptr);
    if (!initD2D(app) || !createRenderTarget(app)) return 3;
    updateTextFormats(app);
    std::ifstream file{std::filesystem::path{argv[1]},std::ios::binary};
    if (!file) { std::cerr << "Fixture could not be opened\n"; return 5; }
    std::stringstream buffer; buffer << file.rdbuf();
    auto parsed = app.parser.parse(buffer.str());
    if (!parsed.success) return 4;
    app.root = parsed.root; app.verticalReading = true;
    std::filesystem::create_directories(argv[2]);
    int failures = 0;
    for (int width : {1024,480}) for (int theme : {0,5}) {
        app.width=width; app.height=768; app.theme=THEMES[theme]; app.verticalOffset=0;
        app.renderTarget->BeginDraw(); renderVerticalReading(app); app.renderTarget->EndDraw();
        if (!app.verticalLayout || app.verticalExtent <= 0) { ++failures; continue; }
        if (app.verticalLayout->GetReadingDirection()!=DWRITE_READING_DIRECTION_TOP_TO_BOTTOM ||
            app.verticalLayout->GetFlowDirection()!=DWRITE_FLOW_DIRECTION_RIGHT_TO_LEFT) ++failures;
        BOOL trailing,inside; DWRITE_HIT_TEST_METRICS hit{};
        FLOAT x,y; app.verticalLayout->HitTestTextPosition(0,FALSE,&x,&y,&hit);
        DWRITE_TEXT_METRICS m{}; app.verticalLayout->GetMetrics(&m);
        float visibleX=width-32-m.left-m.width+x;
        if (visibleX < 32 || visibleX > width-32 || y < 0 || y > 648) ++failures;
        auto prefix=std::filesystem::path{argv[2]}/(std::to_wstring(width)+L"-"+std::to_wstring(theme));
        if (!snapshot(app,prefix.wstring()+L"-first.png")) ++failures;
        for (int i=0;i<1000;++i) turnVerticalPage(app,1);
        const float last=std::floor(std::max(0.0f,app.verticalExtent-0.1f)/app.verticalPageWidth)*app.verticalPageWidth;
        if (std::abs(app.verticalOffset-last)>0.1f) ++failures;
        if (!snapshot(app,prefix.wstring()+L"-last.png")) ++failures;
        for (int i=0;i<1000;++i) turnVerticalPage(app,-1);
        if (app.verticalOffset!=0) ++failures;
        // Exercise real search and page navigation using a repeated Chinese character.
        app.showSearch=true; app.folderSearchEnabled=false; app.searchQuery=L"月";
        performSearch(app);
        app.verticalLayout->GetMetrics(&m);
        if (app.searchMatches.empty()) ++failures;
        for (size_t i=0;i<app.searchMatches.size();++i) {
            app.searchCurrentIndex=(int)i; scrollToCurrentMatch(app);
            FLOAT hx,hy; DWRITE_HIT_TEST_METRICS h{};
            app.verticalLayout->HitTestTextPosition((UINT32)app.searchMatches[i].startPos,FALSE,&hx,&hy,&h);
            const float center=width-32-m.left-m.width+app.verticalOffset+h.left+h.width/2;
            if (center<width-32-app.verticalPageWidth-0.1f || center>width-32+0.1f) ++failures;
        }
        if (verticalReadingKey(app,VK_LEFT) || verticalReadingKey(app,VK_ESCAPE) ||
            verticalReadingKey(app,'F')) ++failures;
        app.renderTarget->BeginDraw(); renderVerticalReading(app); renderSearchOverlay(app);
        renderTabStrip(app); app.renderTarget->EndDraw();
        bool testedPin=false;
        for (const auto& hit : app.tabHits) if (hit.index==-4) {
            int px=(int)((hit.rect.left+hit.rect.right)/2), py=(int)((hit.rect.top+hit.rect.bottom)/2);
            handleMouseDown(app,app.hwnd,0,MAKELPARAM(px,py));
            handleMouseUp(app,app.hwnd,0,MAKELPARAM(px,py));
            if (!app.alwaysOnTop || !(GetWindowLongPtrW(app.hwnd,GWL_EXSTYLE)&WS_EX_TOPMOST)) ++failures;
            handleMouseDown(app,app.hwnd,0,MAKELPARAM(px,py));
            handleMouseUp(app,app.hwnd,0,MAKELPARAM(px,py));
            if (app.alwaysOnTop) ++failures;
            testedPin=true; break;
        }
        if (!testedPin) ++failures;
        app.showSearch=false;
        std::cout << width << " theme=" << theme << " extent=" << app.verticalExtent << " firstX=" << visibleX << '\n';
    }
    DestroyWindow(app.hwnd);
    std::cout << "failures=" << failures << '\n';
    return failures ? 1 : 0;
}

