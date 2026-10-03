#include "vertical_reading.h"
#include "utils.h"
#include "search.h"
#include <dwrite_2.h>

namespace {
void prose(const ElementPtr& node, std::wstring& text) {
    if (!node) return;
    if (node->type == ElementType::RubyText || node->type == ElementType::Properties) return;
    if (node->type == ElementType::SoftBreak || node->type == ElementType::HardBreak) {
        text += L'\n';
        return;
    }
    if (!node->text.empty()) text += toWide(node->text);
    for (const auto& child : node->children) prose(child, text);
    switch (node->type) {
        case ElementType::Paragraph: case ElementType::Heading:
        case ElementType::CodeBlock: case ElementType::ListItem:
        case ElementType::TableRow: case ElementType::BlockQuote:
            if (!text.empty() && text.back() != L'\n') text += L'\n';
            break;
        case ElementType::TableCell: text += L"　"; break;
        default: break;
    }
}
D2D1_RECT_F toggleRect(const App& app) {
    float s = app.contentScale;
    return D2D1::RectF(app.width - 160*s, app.height - 38*s,
                     app.width - 12*s, app.height - 8*s);
}
void toggle(App& app) {
    app.verticalReading = !app.verticalReading;
    if (app.showSearch) { performSearch(app); scrollToCurrentMatch(app); }
    InvalidateRect(app.hwnd, nullptr, FALSE);
}
}

void drawReadingToggle(App& app) {
    if (app.editMode || !app.root) return;
    auto rect = toggleRect(app);
    app.brush->SetColor(app.theme.codeBackground);
    app.renderTarget->FillRectangle(rect, app.brush);
    app.brush->SetColor(app.theme.accent);
    app.renderTarget->DrawRectangle(rect, app.brush);
    const wchar_t* label = app.verticalReading ? L"横排  Ctrl+Shift+V" : L"竖排  Ctrl+Shift+V";
    rect.left += 8*app.contentScale;
    app.renderTarget->DrawText(label, (UINT32)wcslen(label), app.statsFormat ? app.statsFormat : app.textFormat,
                               rect, app.brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
bool readingToggleClick(App& app, int x, int y) {
    if (app.editMode || !app.root) return false;
    auto r = toggleRect(app);
    if (x < r.left || x > r.right || y < r.top || y > r.bottom) return false;
    toggle(app);
    return true;
}
void turnVerticalPage(App& app, int direction) {
    const float last = std::floor(std::max(0.0f, app.verticalExtent-0.1f)/app.verticalPageWidth)*app.verticalPageWidth;
    app.verticalOffset = std::clamp(app.verticalOffset + direction*app.verticalPageWidth, 0.0f, last);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}
bool verticalReadingKey(App& app, WPARAM key) {
    if (!app.editMode && app.root && key == 'V' && (GetKeyState(VK_CONTROL)&0x8000) &&
        (GetKeyState(VK_SHIFT)&0x8000)) { toggle(app); return true; }
    if (!app.verticalReading || app.editMode) return false;
    if (app.showSearch || app.showSettings || app.showHelp || app.showContextMenu ||
        app.showTabSwitcher || app.showTabMenu || app.showThemeChooser ||
        app.showThemeEditor || app.showShortcutEditor || app.showFolderBrowser ||
        app.showToc || app.showPrintPreview || app.confirmExitPending) return false;
    if (GetKeyState(VK_CONTROL)&0x8000) return false;
    switch (key) {
        case VK_LEFT: case VK_NEXT: case VK_SPACE: turnVerticalPage(app, 1); return true;
        case VK_RIGHT: case VK_PRIOR: turnVerticalPage(app, -1); return true;
        case VK_HOME: app.verticalOffset = 0; InvalidateRect(app.hwnd, nullptr, FALSE); return true;
        case VK_END: app.verticalOffset = std::floor(std::max(0.0f,app.verticalExtent-0.1f)/app.verticalPageWidth)*app.verticalPageWidth;
                     InvalidateRect(app.hwnd, nullptr, FALSE); return true;
        case VK_ESCAPE: toggle(app); return true;
        // Avoid invoking horizontal selection/search/edit operations on this view.
        case 'E': return true;
        default: return false;
    }
}
void prepareVerticalReading(App& app) {
    const float s = app.contentScale;
    const float fontSize = 24*s*app.zoomFactor;
    const float top = (app.showSearch ? 120 : 60)*s, bottom = app.height-60*s;
    const float left = 32*s, right = app.width-32*s;
    const float columnWidth = fontSize*1.8f;
    app.verticalPageWidth = std::max(columnWidth, std::floor((right-left)/columnWidth)*columnWidth);
    if (bottom <= top || right <= left) return;
    if (app.verticalRoot != app.root || app.verticalWidth != app.width ||
        app.verticalHeight != app.height || app.verticalFontSize != fontSize || app.verticalContentTop != top) {
        bool newDocument = app.verticalRoot != app.root;
        app.verticalLayout.Reset();
        std::wstring text;
        prose(app.root, text);
        app.verticalText = text;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
        HRESULT hr = app.dwriteFactory->CreateTextFormat(L"Microsoft YaHei", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            fontSize, L"zh-CN", &format);
        if (SUCCEEDED(hr)) {
            format->SetReadingDirection(DWRITE_READING_DIRECTION_TOP_TO_BOTTOM);
            format->SetFlowDirection(DWRITE_FLOW_DIRECTION_RIGHT_TO_LEFT);
            format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, fontSize*1.8f, fontSize);
            hr = app.dwriteFactory->CreateTextLayout(text.c_str(), (UINT32)text.size(), format.Get(),
                10000000.0f, bottom-top, &app.verticalLayout);
        }
        Microsoft::WRL::ComPtr<IDWriteTextLayout2> layout2;
        if (SUCCEEDED(hr) && SUCCEEDED(app.verticalLayout.As(&layout2))) {
            layout2->SetVerticalGlyphOrientation(DWRITE_VERTICAL_GLYPH_ORIENTATION_DEFAULT);
            DWRITE_TEXT_METRICS metrics{};
            app.verticalLayout->GetMetrics(&metrics);
            app.verticalExtent = metrics.width;
        } else app.verticalLayout.Reset();
        app.verticalRoot = app.root;
        app.verticalWidth = app.width; app.verticalHeight = app.height;
        app.verticalFontSize = fontSize;
        app.verticalContentTop = top;
        if (newDocument) app.verticalOffset = 0;
        app.verticalOffset = std::clamp(app.verticalOffset, 0.0f,
            std::floor(std::max(0.0f,app.verticalExtent-0.1f)/app.verticalPageWidth)*app.verticalPageWidth);
    }
}
void renderVerticalReading(App& app) {
    auto rt = app.renderTarget;
    rt->Clear(app.theme.background);
    prepareVerticalReading(app);
    const float s = app.contentScale;
    const float top = app.verticalContentTop, bottom = app.height-60*s;
    const float left = 32*s, right = app.width-32*s;
    app.brush->SetColor(app.theme.text);
    if (app.verticalLayout) {
        DWRITE_TEXT_METRICS metrics{};
        app.verticalLayout->GetMetrics(&metrics);
        rt->PushAxisAlignedClip(D2D1::RectF(std::max(left,right-app.verticalPageWidth), top, right, bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if (app.showSearch) for (size_t i=0; i<app.searchMatches.size(); ++i) {
            const auto& match = app.searchMatches[i];
            UINT32 count=0;
            const float origin = right-metrics.left-metrics.width+app.verticalOffset;
            app.verticalLayout->HitTestTextRange((UINT32)match.startPos, (UINT32)match.length,
                origin, top, nullptr, 0, &count);
            std::vector<DWRITE_HIT_TEST_METRICS> hits(count);
            if (count && SUCCEEDED(app.verticalLayout->HitTestTextRange((UINT32)match.startPos,
                (UINT32)match.length, origin, top, hits.data(), count, &count))) {
                auto color=app.theme.accent; color.a = i==(size_t)app.searchCurrentIndex ? 0.55f : 0.22f;
                app.brush->SetColor(color);
                for (const auto& h : hits) rt->FillRectangle(
                    D2D1::RectF(h.left,h.top,h.left+h.width,h.top+h.height),app.brush);
            }
        }
        app.brush->SetColor(app.theme.text);
        rt->DrawTextLayout(D2D1::Point2F(right-metrics.left-metrics.width+app.verticalOffset, top),
                           app.verticalLayout.Get(), app.brush);
        rt->PopAxisAlignedClip();
    } else {
        const wchar_t* error = L"竖排布局失败，请切回横排。";
        rt->DrawText(error, (UINT32)wcslen(error), app.textFormat, D2D1::RectF(left,top,right,bottom), app.brush);
    }
    const wchar_t* help = L"竖排　← / 空格：下一页　→：上一页　Ctrl+F：搜索";
    rt->DrawText(help, (UINT32)wcslen(help), app.statsFormat ? app.statsFormat : app.textFormat,
                 D2D1::RectF(left, bottom+4*s, right-160*s, bottom+28*s), app.brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    drawReadingToggle(app);
}
