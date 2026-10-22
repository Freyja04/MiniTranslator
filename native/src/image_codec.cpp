#include "image_codec.hpp"

#include "util.hpp"

#include <wincodec.h>
#include <objidl.h>

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
