#include "image_codec.hpp"

#include "util.hpp"

#include <wincodec.h>
#include <objidl.h>

#include <algorithm>
#include <array>

namespace minitrans {

namespace {

template <typename T>
class ComPtr {
public:
    ~ComPtr() { reset(); }
    T** put() {
        reset();
        return &value_;
    }
    T* get() const noexcept { return value_; }
    T* operator->() const noexcept { return value_; }
    void reset() noexcept {
        if (value_) {
            value_->Release();
            value_ = nullptr;
        }
    }

private:
    T* value_{};
};

void check(HRESULT result, std::string_view operation) {
    if (FAILED(result)) {
        throw AppError(std::string(operation) + " failed (HRESULT " +
                       std::to_string(static_cast<unsigned long>(result)) + ")");
    }
}

}  // namespace

PixelBuffer normalize_contrast_bgra(const PixelBuffer& image) {
    if (image.empty()) {
        throw AppError("cannot normalize an empty image");
    }
    // Per-channel histogram stretch over the 1st..99th percentile keeps a
    // stray bright pixel from flattening the range while still pushing washed
    // screenshots to the full [0, 255] luminance span.
    constexpr UINT channels = 3;  // B, G, R - alpha passes through untouched
    std::array<std::array<UINT, 256>, channels> histograms{};
    const std::size_t pixels = static_cast<std::size_t>(image.width) * image.height;
    for (std::size_t i = 0; i < pixels; ++i) {
        const BYTE* px = image.bgra.data() + i * 4;
        ++histograms[0][px[0]];
        ++histograms[1][px[1]];
        ++histograms[2][px[2]];
    }
    std::array<BYTE, 256> lut[channels];
    const UINT low_cut = static_cast<UINT>(pixels * 0.01);
    const UINT high_cut = static_cast<UINT>(pixels * 0.01);
    for (UINT channel = 0; channel < channels; ++channel) {
        UINT accumulator = 0;
        int low = 0;
        int high = 255;
        for (int value = 0; value < 256; ++value) {
            accumulator += histograms[channel][value];
            if (accumulator > low_cut) {
                low = value;
                break;
            }
        }
        accumulator = 0;
        for (int value = 255; value >= 0; --value) {
            accumulator += histograms[channel][value];
            if (accumulator > high_cut) {
                high = value;
                break;
            }
        }
        if (high - low < 8) {
            // Nearly flat channel: stretching would amplify noise, keep it.
            for (int value = 0; value < 256; ++value) {
                lut[channel][value] = static_cast<BYTE>(value);
            }
            continue;
        }
        const double scale = 255.0 / (high - low);
        for (int value = 0; value < 256; ++value) {
            const int stretched =
                static_cast<int>((value - low) * scale + 0.5);
            lut[channel][value] = static_cast<BYTE>(
                std::clamp(stretched, 0, 255));
        }
    }
    PixelBuffer result = image;
    for (std::size_t i = 0; i < pixels; ++i) {
        BYTE* px = result.bgra.data() + i * 4;
        px[0] = lut[0][px[0]];
        px[1] = lut[1][px[1]];
        px[2] = lut[2][px[2]];
    }
    return result;
}

PixelBuffer resize_bgra(const PixelBuffer& image, int width, int height) {
    if (image.empty() || width <= 0 || height <= 0) {
        throw AppError("cannot resize an empty image");
    }
    ComPtr<IWICImagingFactory> factory;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(factory.put())), "create WIC factory");
    ComPtr<IWICBitmap> bitmap;
    check(factory->CreateBitmapFromMemory(
              static_cast<UINT>(image.width), static_cast<UINT>(image.height),
              GUID_WICPixelFormat32bppBGRA, static_cast<UINT>(image.stride),
              static_cast<UINT>(image.bgra.size()),
              const_cast<BYTE*>(image.bgra.data()), bitmap.put()),
          "create WIC bitmap");
    ComPtr<IWICBitmapScaler> scaler;
    check(factory->CreateBitmapScaler(scaler.put()), "create WIC scaler");
    check(scaler->Initialize(bitmap.get(), static_cast<UINT>(width),
                             static_cast<UINT>(height), WICBitmapInterpolationModeFant),
          "resize image");
    PixelBuffer result;
    result.width = width;
    result.height = height;
    result.stride = width * 4;
    result.bgra.resize(static_cast<std::size_t>(result.stride) * result.height);
    check(scaler->CopyPixels(nullptr, static_cast<UINT>(result.stride),
                             static_cast<UINT>(result.bgra.size()), result.bgra.data()),
          "copy resized pixels");
    return result;
}

}  // namespace minitrans
