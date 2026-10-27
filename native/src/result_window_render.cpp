#include "result_window.hpp"
#include "result_window_internal.hpp"

#include "theme.hpp"
#include "ui_draw.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace minitrans {

using result_window_internal::dpi_value;

namespace {

void alpha_fill(HDC destination, const RECT& rect, COLORREF color, BYTE alpha) {
    if (!destination || rect.right <= rect.left || rect.bottom <= rect.top || alpha == 0) return;
    HDC source = CreateCompatibleDC(destination);
    HBITMAP bitmap = CreateCompatibleBitmap(destination, 1, 1);
    if (!source || !bitmap) {
        if (bitmap) DeleteObject(bitmap);
        if (source) DeleteDC(source);
        return;
    }
    const HGDIOBJ old = SelectObject(source, bitmap);
    SetPixelV(source, 0, 0, color);
    BLENDFUNCTION blend{AC_SRC_OVER, 0, alpha, 0};
    AlphaBlend(destination, rect.left, rect.top, rect.right - rect.left,
               rect.bottom - rect.top, source, 0, 0, 1, 1, blend);
    SelectObject(source, old);
    DeleteObject(bitmap);
    DeleteDC(source);
}

void alpha_fill_region(HDC destination, HRGN region, const RECT& bounds,
                       COLORREF color, BYTE alpha) {
    if (!destination || !region || alpha == 0 || bounds.right <= bounds.left
        || bounds.bottom <= bounds.top) {
        return;
    }
    const int saved = SaveDC(destination);
    if (saved == 0) return;
    ExtSelectClipRgn(destination, region, RGN_AND);
    alpha_fill(destination, bounds, color, alpha);
    RestoreDC(destination, saved);
}

void alpha_frame(HDC destination, const RECT& bounds, int thickness,
                 COLORREF color, BYTE alpha) {
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0) return;
    thickness = std::clamp(thickness, 1, std::max(1, std::min(width, height) / 2));
    HRGN frame = CreateRectRgn(bounds.left, bounds.top, bounds.right, bounds.bottom);
    if (!frame) return;
    if (width > thickness * 2 && height > thickness * 2) {
        HRGN middle = CreateRectRgn(
            bounds.left + thickness, bounds.top + thickness,
            bounds.right - thickness, bounds.bottom - thickness);
        if (middle) {
            CombineRgn(frame, frame, middle, RGN_DIFF);
            DeleteObject(middle);
        }
    }
    alpha_fill_region(destination, frame, bounds, color, alpha);
    DeleteObject(frame);
}

void draw_alpha_handle(HDC destination, const RECT& bounds, COLORREF accent,
                       double opacity, int corner_radius) {
    const double clamped = std::clamp(opacity, 0.0, 1.0);
    if (clamped <= 0.0 || bounds.right <= bounds.left || bounds.bottom <= bounds.top) return;
    const int ellipse = std::max(2, corner_radius * 2);
    HRGN outer = CreateRoundRectRgn(bounds.left, bounds.top, bounds.right, bounds.bottom,
                                    ellipse, ellipse);
    if (!outer) return;
    alpha_fill_region(destination, outer, bounds, accent,
                      static_cast<BYTE>(std::lround(clamped * 255.0)));

    HRGN edge = CreateRoundRectRgn(bounds.left, bounds.top, bounds.right, bounds.bottom,
                                   ellipse, ellipse);
    if (edge && bounds.right - bounds.left > 2 && bounds.bottom - bounds.top > 2) {
        HRGN middle = CreateRoundRectRgn(
            bounds.left + 1, bounds.top + 1, bounds.right - 1, bounds.bottom - 1,
            std::max(2, ellipse - 2), std::max(2, ellipse - 2));
        if (middle) {
            CombineRgn(edge, edge, middle, RGN_DIFF);
            DeleteObject(middle);
        }
    }
    if (edge) {
        alpha_fill_region(destination, edge, bounds, RGB(255, 255, 255),
                          static_cast<BYTE>(std::lround(clamped * 215.0)));
        DeleteObject(edge);
    }
    DeleteObject(outer);
}

struct SizeBadge {
    std::wstring text;
    RECT bounds{};
    int font_pixels{};
};

SizeBadge make_size_badge(HWND window, int width, int height) {
    const UINT dpi = GetDpiForWindow(window);
    const int safe_dpi = static_cast<int>(dpi ? dpi : 96);
    SizeBadge badge;
    badge.text = std::to_wstring(MulDiv(width, 96, safe_dpi)) + L" × "
               + std::to_wstring(MulDiv(height, 96, safe_dpi));
    badge.font_pixels = dpi_value(window, 11);
    SIZE measured{};
    HDC dc = GetDC(window);
    HFONT font = CreateFontW(-badge.font_pixels, 0, 0, 0, FW_NORMAL,
                             FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH,
                             theme::font_family.c_str());
    if (dc && font) {
        const HGDIOBJ old_font = SelectObject(dc, font);
        GetTextExtentPoint32W(dc, badge.text.c_str(),
                              static_cast<int>(badge.text.size()), &measured);
        SelectObject(dc, old_font);
    }
    if (font) DeleteObject(font);
    if (dc) ReleaseDC(window, dc);
    const int top = dpi_value(window, 6);
    badge.bounds = {
        0, top,
        std::min(width, static_cast<int>(measured.cx) + dpi_value(window, 14)),
        std::min(height, top + static_cast<int>(measured.cy) + dpi_value(window, 7)),
    };
    return badge;
}

}  // namespace

namespace result_window_internal {

ID2D1Factory* d2d_factory() noexcept {
    // The result window's own rendering and the shared rounded-rect helper
    // both draw through one factory.
    return ui_draw::d2d_factory();
}

ID2D1StrokeStyle* round_stroke_style() noexcept {
    static ID2D1StrokeStyle* style = [] {
        auto* factory = d2d_factory();
        if (!factory) return static_cast<ID2D1StrokeStyle*>(nullptr);
        D2D1_STROKE_STYLE_PROPERTIES properties{};
        properties.startCap = D2D1_CAP_STYLE_ROUND;
        properties.endCap = D2D1_CAP_STYLE_ROUND;
        properties.dashCap = D2D1_CAP_STYLE_ROUND;
        properties.lineJoin = D2D1_LINE_JOIN_ROUND;
        properties.miterLimit = 2.0F;
        properties.dashStyle = D2D1_DASH_STYLE_SOLID;
        ID2D1StrokeStyle* value = nullptr;
        factory->CreateStrokeStyle(properties, nullptr, 0, &value);
        return value;
    }();
    return style;
}

D2D1_COLOR_F d2d_color(COLORREF color, float alpha) noexcept {
    return D2D1::ColorF(
        static_cast<float>(GetRValue(color)) / 255.0F,
        static_cast<float>(GetGValue(color)) / 255.0F,
        static_cast<float>(GetBValue(color)) / 255.0F,
        std::clamp(alpha, 0.0F, 1.0F));
}

void draw_control_icon(ID2D1RenderTarget* target, int index,
                       const D2D1_RECT_F& bounds, COLORREF color,
                       float opacity) {
    if (!target || index < 0 || index >= 4) return;
    ID2D1SolidColorBrush* brush = nullptr;
    if (FAILED(target->CreateSolidColorBrush(d2d_color(color, opacity), &brush))) return;

    const float scale = std::min(bounds.right - bounds.left,
                                 bounds.bottom - bounds.top) / 24.0F;
    if (scale <= 0.0F) {
        brush->Release();
        return;
    }
    const float origin_x = (bounds.left + bounds.right) * 0.5F - 12.0F * scale;
    const float origin_y = (bounds.top + bounds.bottom) * 0.5F - 12.0F * scale;
    const auto point = [&](float x, float y) {
        return D2D1::Point2F(origin_x + x * scale, origin_y + y * scale);
    };
    const float adjusted_weight = 2.1F *
        (scale >= 0.62F ? 1.0F : std::pow(0.62F / scale, 0.45F));
    const float stroke = adjusted_weight * scale;
    auto* stroke_style = round_stroke_style();
    const auto line = [&](float x1, float y1, float x2, float y2) {
        target->DrawLine(point(x1, y1), point(x2, y2), brush, stroke, stroke_style);
    };

    if (index == 0) {
        constexpr std::array<D2D1_POINT_2F, 5> body{{
            {5.2F, 15.4F}, {14.8F, 5.8F}, {18.2F, 9.2F},
            {8.6F, 18.8F}, {4.2F, 19.8F},
        }};
        for (std::size_t i = 1; i < body.size(); ++i) {
            line(body[i - 1].x, body[i - 1].y, body[i].x, body[i].y);
        }
        line(5.2F, 15.4F, 8.6F, 18.8F);
        line(13.3F, 7.3F, 16.7F, 10.7F);
    } else if (index == 1) {
        constexpr float radius = 6.4F;
        constexpr float radians = 105.0F * 3.14159265358979323846F / 180.0F;
        constexpr int arc_segments = 4;
        constexpr float segment_angle =
            75.0F * 3.14159265358979323846F / 180.0F;
        const float bezier_factor =
            4.0F / 3.0F * std::tan(segment_angle / 4.0F);
        const auto arc_point = [&](float angle) {
            return D2D1::Point2F(12.0F + radius * std::cos(angle),
                                12.0F - radius * std::sin(angle));
        };
        const auto start = arc_point(radians);
        ID2D1PathGeometry* geometry = nullptr;
        ID2D1GeometrySink* sink = nullptr;
        if (auto* factory = d2d_factory(); factory &&
            SUCCEEDED(factory->CreatePathGeometry(&geometry)) &&
            SUCCEEDED(geometry->Open(&sink))) {
            sink->BeginFigure(point(start.x, start.y), D2D1_FIGURE_BEGIN_HOLLOW);
            for (int segment = 0; segment < arc_segments; ++segment) {
                const float angle0 = radians + segment * segment_angle;
                const float angle1 = angle0 + segment_angle;
                const auto end = arc_point(angle1);
                const auto control1 = D2D1::Point2F(
                    12.0F + radius * std::cos(angle0)
                        - radius * std::sin(angle0) * bezier_factor,
                    12.0F - radius * std::sin(angle0)
                        - radius * std::cos(angle0) * bezier_factor);
                const auto control2 = D2D1::Point2F(
                    end.x + radius * std::sin(angle1) * bezier_factor,
                    end.y + radius * std::cos(angle1) * bezier_factor);
                sink->AddBezier(D2D1::BezierSegment(
                    point(control1.x, control1.y),
                    point(control2.x, control2.y),
                    point(end.x, end.y)));
            }
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            target->DrawGeometry(geometry, brush, stroke, stroke_style);
        }
        if (sink) sink->Release();
        if (geometry) geometry->Release();

        const float px = 12.0F + radius * std::cos(radians);
        const float py = 12.0F - radius * std::sin(radians);
        const float tx = std::sin(radians);
        const float ty = std::cos(radians);
        const float nx = -ty;
        const float ny = tx;
        ID2D1PathGeometry* arrow = nullptr;
        ID2D1GeometrySink* arrow_sink = nullptr;
        if (auto* factory = d2d_factory(); factory &&
            SUCCEEDED(factory->CreatePathGeometry(&arrow)) &&
            SUCCEEDED(arrow->Open(&arrow_sink))) {
            arrow_sink->BeginFigure(point(px + tx * 4.2F, py + ty * 4.2F),
                                    D2D1_FIGURE_BEGIN_FILLED);
            arrow_sink->AddLine(point(px - tx * 0.8F + nx * 2.9F,
                                      py - ty * 0.8F + ny * 2.9F));
            arrow_sink->AddLine(point(px - tx * 0.8F - nx * 2.9F,
                                      py - ty * 0.8F - ny * 2.9F));
            arrow_sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            arrow_sink->Close();
            target->FillGeometry(arrow, brush);
        }
        if (arrow_sink) arrow_sink->Release();
        if (arrow) arrow->Release();
    } else if (index == 2) {
        line(7.0F, 12.0F, 17.0F, 12.0F);
    } else {
        line(7.5F, 7.5F, 16.5F, 16.5F);
        line(16.5F, 7.5F, 7.5F, 16.5F);
    }
    brush->Release();
}

}  // namespace result_window_internal

using result_window_internal::resize_transparent_key;

void ResultWindow::update_window_region() {
    if (!window_) return;
    // A changing fragmented HRGN exposes desktop holes while the DWM resize loop
    // is still presenting older frames. Keep one rectangular surface and repaint
    // it atomically instead.
    SetWindowRgn(window_, nullptr, FALSE);
}

HDC ResultWindow::ensure_paint_buffer(HDC target, int width, int height) {
    if (!target || width <= 0 || height <= 0) return nullptr;
    if (!paint_buffer_dc_) {
        paint_buffer_dc_ = CreateCompatibleDC(target);
        if (!paint_buffer_dc_) return nullptr;
    }
    if (paint_buffer_bitmap_ && width <= paint_buffer_width_ &&
        height <= paint_buffer_height_) {
        return paint_buffer_dc_;
    }

    const int grown_width = std::max(width, paint_buffer_width_ + paint_buffer_width_ / 2);
    const int grown_height = std::max(height, paint_buffer_height_ + paint_buffer_height_ / 2);
    HBITMAP replacement = CreateCompatibleBitmap(target, grown_width, grown_height);
    if (!replacement) return nullptr;
    const HGDIOBJ previous = SelectObject(paint_buffer_dc_, replacement);
    if (!previous || previous == HGDI_ERROR) {
        DeleteObject(replacement);
        return nullptr;
    }
    if (!paint_buffer_original_) paint_buffer_original_ = previous;
    if (paint_buffer_bitmap_) DeleteObject(paint_buffer_bitmap_);
    paint_buffer_bitmap_ = replacement;
    paint_buffer_width_ = grown_width;
    paint_buffer_height_ = grown_height;
    return paint_buffer_dc_;
}

void ResultWindow::release_paint_buffer() noexcept {
    if (paint_buffer_dc_ && paint_buffer_original_) {
        SelectObject(paint_buffer_dc_, paint_buffer_original_);
    }
    if (paint_buffer_bitmap_) DeleteObject(paint_buffer_bitmap_);
    if (paint_buffer_dc_) DeleteDC(paint_buffer_dc_);
    paint_buffer_dc_ = nullptr;
    paint_buffer_bitmap_ = nullptr;
    paint_buffer_original_ = nullptr;
    paint_buffer_width_ = 0;
    paint_buffer_height_ = 0;
}

void ResultWindow::build_repaired_background() {
    translated_surface_ = {};
    if (!original_ || original_->empty()) return;
    auto prepared = prepare_result_render(
        *original_, result_.blocks, appearance_.font_family,
        appearance_.minimum_font_pixels);
    translated_surface_ = std::move(prepared.translated);
}

void ResultWindow::paint() {
    PAINTSTRUCT state{};
    HDC target = BeginPaint(window_, &state);
    if (!target) return;
    RECT client{};
    GetClientRect(window_, &client);
    const int client_width = static_cast<int>(client.right - client.left);
    const int client_height = static_cast<int>(client.bottom - client.top);
    HDC buffer = ensure_paint_buffer(target, client_width, client_height);
    HDC dc = buffer ? buffer : target;
    HBRUSH empty_background = CreateSolidBrush(
        resizing_ ? resize_transparent_key : RGB(18, 20, 24));
    FillRect(dc, &client, empty_background);
    DeleteObject(empty_background);
    const auto draw_surface = [&](const PixelBuffer& surface,
                                  int destination_x, int destination_y,
                                  int source_x, int source_y,
                                  int width, int height) {
        if (surface.empty() || width <= 0 || height <= 0) return;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = surface.width;
        info.bmiHeader.biHeight = -surface.height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchDIBits(dc, destination_x, destination_y, width, height,
                      source_x, source_y, width, height,
                      surface.bgra.data(), &info, DIB_RGB_COLORS, SRCCOPY);
    };
    RECT window_rect{};
    GetWindowRect(window_, &window_rect);
    if (resizing_) {
        // The color-keyed interior reveals the compositor's live desktop.
    } else if (pending_source_ && pending_source_->original
               && !pending_source_->original->empty()) {
        draw_surface(*pending_source_->original,
                     pending_source_->image_origin.x - window_rect.left,
                     pending_source_->image_origin.y - window_rect.top,
                     0, 0, pending_source_->original->width,
                     pending_source_->original->height);
    } else if (!refresh_surface_.empty()) {
        draw_surface(refresh_surface_, 0, 0, 0, 0,
                     refresh_surface_.width, refresh_surface_.height);
    } else {
        const PixelBuffer* display = nullptr;
        if (!peek_ && !translated_surface_.empty()) {
            display = &translated_surface_;
        } else if (original_ && !original_->empty()) {
            display = original_.get();
        }
        if (display) {
        const int offset_x = image_screen_origin_.x - window_rect.left;
        const int offset_y = image_screen_origin_.y - window_rect.top;
            draw_surface(*display, offset_x, offset_y, 0, 0,
                         display->width, display->height);
        }
    }

    if (!resizing_ && (loading_ || refreshing_)) {
        alpha_fill(dc, client, RGB(0, 0, 0), 46);
        const int progress_width = std::min(
            client_width,
            std::max(dpi_value(window_, 28), client_width / 4));
        const int travel = std::max(1, client_width - progress_width);
        const double cycle = progress_phase_ * 2.0;
        const double position = cycle <= 1.0 ? cycle : 2.0 - cycle;
        const double eased = position * position * (3.0 - 2.0 * position);
        const int left = static_cast<int>(std::lround(eased * travel));
        RECT progress{left, 0, std::min(client_width, left + progress_width),
                      std::min(client_height, dpi_value(window_, 2))};
        HBRUSH accent = CreateSolidBrush(theme::accent);
        FillRect(dc, &progress, accent);
        DeleteObject(accent);
    } else if (!resizing_ && !error_.empty()) {
        alpha_fill(dc, client, RGB(24, 24, 28), 232);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(240, 240, 245));
        const int font_pixels = std::clamp(client_height / 6,
                                           dpi_value(window_, 11),
                                           dpi_value(window_, 14));
        HFONT font = CreateFontW(-font_pixels, 0, 0, 0, FW_NORMAL,
                                 FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH,
                                 appearance_.font_family.c_str());
        const HGDIOBJ old_font = SelectObject(dc, font);
        const int horizontal = dpi_value(window_, 10);
        const int vertical = dpi_value(window_, 8);
        RECT text_rect{client.left + horizontal, client.top + vertical,
                       client.right - horizontal, client.bottom - vertical};
        DrawTextW(dc, error_.c_str(), -1, &text_rect,
                  DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, old_font);
        DeleteObject(font);
    }

    const int width = client_width;
    const int height = client_height;
    if (width > 0 && height > 0 && !freeze_content_only_) {
        const int border = std::min(dpi_value(window_, 2), std::max(1, std::min(width, height) / 2));
        const int dark_border = std::min(dpi_value(window_, 1),
                                         std::max(1, std::min(width, height) / 2));
        const RECT frame_bounds{0, 0, width, height};
        alpha_frame(dc, frame_bounds, dark_border, RGB(0, 0, 0), 90);
        const RECT inner_dark{border, border, width - border, height - border};
        if (inner_dark.right > inner_dark.left && inner_dark.bottom > inner_dark.top) {
            alpha_frame(dc, inner_dark, dark_border, RGB(0, 0, 0), 90);
        }
        alpha_frame(dc, frame_bounds, border, theme::accent, 255);

        const int handle = dpi_value(window_, 8);
        if (handles_alpha_ > 0.04 && width >= dpi_value(window_, 56)
            && height >= dpi_value(window_, 34)) {
            const int xs[3]{0, (width - handle) / 2, width - handle};
            const int ys[3]{0, (height - handle) / 2, height - handle};
            constexpr std::array<std::pair<int, int>, 8> positions{{
                {0, 0}, {1, 0}, {2, 0}, {0, 1},
                {2, 1}, {0, 2}, {1, 2}, {2, 2},
            }};
            const int corner = dpi_value(window_, 2);
            for (const auto [column, row] : positions) {
                const RECT handle_bounds{xs[column], ys[row],
                                         xs[column] + handle, ys[row] + handle};
                draw_alpha_handle(dc, handle_bounds, theme::accent,
                                  handles_alpha_, corner);
            }
        }

        if (resizing_) {
            const auto badge = make_size_badge(window_, width, height);
            HFONT badge_font = CreateFontW(-badge.font_pixels, 0, 0, 0, FW_NORMAL,
                                            FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                            CLEARTYPE_QUALITY, DEFAULT_PITCH,
                                            theme::font_family.c_str());
            const HGDIOBJ old_badge_font = SelectObject(dc, badge_font);
            const int badge_height = badge.bounds.bottom - badge.bounds.top;
            HRGN badge_region = CreateRoundRectRgn(
                badge.bounds.left, badge.bounds.top, badge.bounds.right,
                badge.bounds.bottom, badge_height, badge_height);
            if (badge_region) {
                alpha_fill_region(dc, badge_region, badge.bounds, RGB(18, 20, 24), 232);
                DeleteObject(badge_region);
            }
            const int red = std::min(255, static_cast<int>(GetRValue(theme::accent)) * 118 / 100);
            const int green = std::min(255, static_cast<int>(GetGValue(theme::accent)) * 118 / 100);
            const int blue = std::min(255, static_cast<int>(GetBValue(theme::accent)) * 118 / 100);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(red, green, blue));
            RECT badge_text = badge.bounds;
            DrawTextW(dc, badge.text.c_str(), -1, &badge_text,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(dc, old_badge_font);
            DeleteObject(badge_font);
        }
    }
    if (buffer) {
        BitBlt(target, 0, 0, client_width, client_height, buffer, 0, 0, SRCCOPY);
    }
    EndPaint(window_, &state);
}

}  // namespace minitrans
