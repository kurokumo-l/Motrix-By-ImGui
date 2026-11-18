module;

// clang-format off
#include <windows.h>
#include <dwrite_2.h>
#include <imgui.h>
#include <imgui_internal.h>
// clang-format on

module motrix.ui;

import std;
import motrix.core;
import :fonts_dwrite;

namespace motrix::ui {

namespace {

template <typename T> void SafeRelease(T*& ptr) noexcept {
    if (ptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

struct FontMemoryKey {
    const void* data = nullptr;
    UINT64 size = 0;
};

template <typename Derived, typename Interface> class ComBase : public Interface {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override {
        if (!ppv) {
            return E_POINTER;
        }
        if (iid == __uuidof(IUnknown) || iid == __uuidof(Interface)) {
            *ppv = static_cast<Interface*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return refCount_.fetch_add(1, std::memory_order_relaxed) + 1u;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG refs = refCount_.fetch_sub(1, std::memory_order_acq_rel) - 1u;
        if (refs == 0u) {
            delete static_cast<Derived*>(this);
        }
        return refs;
    }

protected:
    virtual ~ComBase() = default;

private:
    std::atomic<ULONG> refCount_{1};
};

class MemoryFontFileStream final : public ComBase<MemoryFontFileStream, IDWriteFontFileStream> {
public:
    MemoryFontFileStream(const void* data, UINT64 size) noexcept
        : data_(static_cast<const std::uint8_t*>(data)), size_(size) {}

    HRESULT STDMETHODCALLTYPE ReadFileFragment(void const** fragmentStart,
                                               UINT64 fileOffset,
                                               UINT64 fragmentSize,
                                               void** fragmentContext) override {
        if (!fragmentStart || !fragmentContext) {
            return E_POINTER;
        }
        if (!data_ || fileOffset > size_ || fragmentSize > size_ - fileOffset) {
            return E_INVALIDARG;
        }
        *fragmentStart = data_ + fileOffset;
        *fragmentContext = nullptr;
        return S_OK;
    }

    void STDMETHODCALLTYPE ReleaseFileFragment(void* /*fragmentContext*/) override {}

    HRESULT STDMETHODCALLTYPE GetFileSize(UINT64* fileSize) override {
        if (!fileSize) {
            return E_POINTER;
        }
        *fileSize = size_;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetLastWriteTime(UINT64* lastWriteTime) override {
        if (!lastWriteTime) {
            return E_POINTER;
        }
        *lastWriteTime = 0;
        return E_NOTIMPL;
    }

private:
    const std::uint8_t* data_ = nullptr;
    UINT64 size_ = 0;
};

class MemoryFontFileLoader final : public ComBase<MemoryFontFileLoader, IDWriteFontFileLoader> {
public:
    HRESULT STDMETHODCALLTYPE CreateStreamFromKey(void const* keyBytes,
                                                  UINT32 keySize,
                                                  IDWriteFontFileStream** stream) override {
        if (!stream) {
            return E_POINTER;
        }
        *stream = nullptr;
        if (!keyBytes || keySize != sizeof(FontMemoryKey)) {
            return E_INVALIDARG;
        }
        FontMemoryKey key{};
        std::memcpy(&key, keyBytes, sizeof(key));
        if (!key.data || key.size == 0) {
            return E_INVALIDARG;
        }
        *stream = new MemoryFontFileStream(key.data, key.size);
        return S_OK;
    }
};

struct DWriteFaceBuilder {
    IDWriteFactory* factory = nullptr;
    IDWriteFontCollection* sysFonts = nullptr;
    MemoryFontFileLoader* loader = nullptr;
};

struct ParsedSysFontSpec {
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    std::string_view familyUtf8;
};

[[nodiscard]] bool ParseSysFontSpec(std::string_view spec, ParsedSysFontSpec& out) noexcept {
    constexpr std::string_view kPrefix = "MOTRIX_SYS:";
    if (!spec.starts_with(kPrefix)) {
        return false;
    }
    const std::string_view rest = spec.substr(kPrefix.size());
    const std::size_t sep = rest.find(':');
    if (sep == std::string_view::npos || sep == 0 || sep + 1 >= rest.size()) {
        return false;
    }
    int wVal = 400;
    const auto [ptr, ec] = std::from_chars(rest.data(), rest.data() + sep, wVal);
    if (ec != std::errc{} || ptr != rest.data() + sep) {
        return false;
    }
    out.weight = static_cast<DWRITE_FONT_WEIGHT>(std::clamp(wVal, 100, 950));
    out.familyUtf8 = motrix::core::TrimAscii(rest.substr(sep + 1));
    return !out.familyUtf8.empty();
}

void* CreateDWriteFaceBuilder() {
    auto b = std::make_unique<DWriteFaceBuilder>();
    if (FAILED(::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                     __uuidof(IDWriteFactory),
                                     reinterpret_cast<IUnknown**>(&b->factory)))) {
        return nullptr;
    }
    b->loader = new MemoryFontFileLoader();
    if (FAILED(b->factory->RegisterFontFileLoader(b->loader))) {
        SafeRelease(b->loader);
        SafeRelease(b->factory);
        return nullptr;
    }
    (void)b->factory->GetSystemFontCollection(&b->sysFonts, FALSE);
    return b.release();
}

void DestroyDWriteFaceBuilder(void* builderRaw) noexcept {
    auto* b = static_cast<DWriteFaceBuilder*>(builderRaw);
    if (!b) {
        return;
    }
    if (b->factory && b->loader) {
        b->factory->UnregisterFontFileLoader(b->loader);
    }
    SafeRelease(b->loader);
    SafeRelease(b->sysFonts);
    SafeRelease(b->factory);
    delete b;
}

void* CreateDWriteMemoryFontFace(void* builderRaw,
                                 const void* data,
                                 std::uint64_t size,
                                 std::uint32_t fontNo) {
    auto* b = static_cast<DWriteFaceBuilder*>(builderRaw);
    if (!b || !b->factory || !b->loader || !data || size == 0) {
        return nullptr;
    }
    const FontMemoryKey key{data, size};
    IDWriteFontFile* fontFile = nullptr;
    if (FAILED(b->factory->CreateCustomFontFileReference(&key, sizeof(key), b->loader, &fontFile))) {
        return nullptr;
    }

    BOOL supported = FALSE;
    DWRITE_FONT_FILE_TYPE fileType = DWRITE_FONT_FILE_TYPE_UNKNOWN;
    DWRITE_FONT_FACE_TYPE faceType = DWRITE_FONT_FACE_TYPE_UNKNOWN;
    UINT32 numFaces = 0;
    const HRESULT analyzeHr = fontFile->Analyze(&supported, &fileType, &faceType, &numFaces);
    if (FAILED(analyzeHr) || !supported || faceType == DWRITE_FONT_FACE_TYPE_UNKNOWN
        || fontNo >= numFaces) {
        SafeRelease(fontFile);
        return nullptr;
    }

    IDWriteFontFace* face = nullptr;
    IDWriteFontFile* files[] = {fontFile};
    const HRESULT faceHr =
        b->factory->CreateFontFace(faceType, 1, files, fontNo, DWRITE_FONT_SIMULATIONS_NONE, &face);
    SafeRelease(fontFile);
    return SUCCEEDED(faceHr) ? face : nullptr;
}

void* CreateDWriteSystemFontFace(void* builderRaw, std::string_view specUtf8) {
    auto* b = static_cast<DWriteFaceBuilder*>(builderRaw);
    ParsedSysFontSpec parsed{};
    if (!b || !b->sysFonts || !ParseSysFontSpec(specUtf8, parsed)) {
        return nullptr;
    }

    const std::wstring wideName = motrix::core::PathFromUtf8(parsed.familyUtf8).wstring();
    UINT32 familyIndex = 0;
    BOOL exists = FALSE;
    if (FAILED(b->sysFonts->FindFamilyName(wideName.c_str(), &familyIndex, &exists)) || !exists) {
        return nullptr;
    }

    IDWriteFontFamily* family = nullptr;
    if (FAILED(b->sysFonts->GetFontFamily(familyIndex, &family)) || !family) {
        return nullptr;
    }
    IDWriteFont* font = nullptr;
    const HRESULT matchHr = family->GetFirstMatchingFont(
        parsed.weight, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font);
    SafeRelease(family);
    if (FAILED(matchHr) || !font) {
        SafeRelease(font);
        return nullptr;
    }

    IDWriteFontFace* face = nullptr;
    const HRESULT faceHr = font->CreateFontFace(&face);
    SafeRelease(font);
    return SUCCEEDED(faceHr) ? face : nullptr;
}

template <typename T> class ComRef {
public:
    ComRef() noexcept = default;
    ~ComRef() { Reset(); }

    ComRef(const ComRef&) = delete;
    ComRef& operator=(const ComRef&) = delete;

    [[nodiscard]] T* Get() const noexcept { return ptr_; }
    [[nodiscard]] T** Put() noexcept {
        Reset();
        return &ptr_;
    }
    [[nodiscard]] T* Detach() noexcept {
        T* out = ptr_;
        ptr_ = nullptr;
        return out;
    }
    T* operator->() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    void Adopt(T* ptr) noexcept {
        Reset();
        ptr_ = ptr;
    }

    void Reset() noexcept {
        if (ptr_) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

private:
    T* ptr_ = nullptr;
};

struct DWriteAtlasState {
    void* faceBuilder = nullptr;
    IDWriteFactory* factory = nullptr;
    IDWriteFactory2* factory2 = nullptr;
    std::array<std::uint8_t, 256> contrastLut{};
    std::vector<BYTE> rawScratch;
    std::vector<unsigned char> alphaScratch;
};

struct DWriteFontSrcData {
    IDWriteFontFace* face = nullptr;
    DWRITE_FONT_METRICS metrics{};
    float scalePerUnit = 0.0f;
    float capRatio = 0.0f;
    UINT16 glyphCount = 0;
};

struct RasterMetrics {
    float fontEmSize = 0.0f;
    float advanceX = 0.0f;
    float rasterizerDensity = 1.0f;
};

void InitContrastLut(std::array<std::uint8_t, 256>& lut) noexcept {
    constexpr float kUnormContrastGamma = 0.82f;
    lut[0] = 0;
    lut[255] = 255;
    for (int i = 1; i < 255; ++i) {
        const float norm = static_cast<float>(i) / 255.0f;
        const long mapped = std::lround(std::pow(norm, kUnormContrastGamma) * 255.0f);
        lut[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(std::clamp(mapped, 0L, 255L));
    }
}

[[nodiscard]] float ComputeEffectiveFontSize(const ImFontConfig* src,
                                             const ImFontBaked* baked) noexcept {
    float size = baked->Size;
    const float refSize = baked->OwnerFont->Sources[0]->SizePixels;
    if (src->MergeMode && src->SizePixels != 0.0f && refSize > 0.0f) {
        size *= (src->SizePixels / refSize);
    }
    return size * src->ExtraSizeScale;
}

[[nodiscard]] bool CreateGlyphAnalysis(const DWriteAtlasState& state,
                                       IDWriteFontFace* face,
                                       UINT16 glyphId,
                                       float fontEmSize,
                                       ComRef<IDWriteGlyphRunAnalysis>& out) noexcept {
    DWRITE_GLYPH_RUN run{};
    run.fontFace = face;
    run.fontEmSize = fontEmSize;
    run.glyphCount = 1;
    run.glyphIndices = &glyphId;
    if (state.factory2) {
        return SUCCEEDED(state.factory2->CreateGlyphRunAnalysis(&run,
                                                                nullptr,
                                                                DWRITE_RENDERING_MODE_NATURAL,
                                                                DWRITE_MEASURING_MODE_NATURAL,
                                                                DWRITE_GRID_FIT_MODE_ENABLED,
                                                                DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE,
                                                                0.0f,
                                                                0.0f,
                                                                out.Put()));
    }
    return SUCCEEDED(state.factory->CreateGlyphRunAnalysis(&run,
                                                           1.0f,
                                                           nullptr,
                                                           DWRITE_RENDERING_MODE_NATURAL,
                                                           DWRITE_MEASURING_MODE_NATURAL,
                                                           0.0f,
                                                           0.0f,
                                                           out.Put()));
}

[[nodiscard]] bool RasterizeAnalysisAlpha(IDWriteGlyphRunAnalysis* analysis,
                                          DWriteAtlasState& state,
                                          RECT& bounds,
                                          DWRITE_TEXTURE_TYPE& texType) {
    const bool useGrayscale = state.factory2 != nullptr;
    texType = useGrayscale ? DWRITE_TEXTURE_ALIASED_1x1 : DWRITE_TEXTURE_CLEARTYPE_3x1;
    if (FAILED(analysis->GetAlphaTextureBounds(texType, &bounds))) {
        return false;
    }
    if (!useGrayscale && (bounds.right <= bounds.left || bounds.bottom <= bounds.top)) {
        texType = DWRITE_TEXTURE_ALIASED_1x1;
        if (FAILED(analysis->GetAlphaTextureBounds(texType, &bounds))) {
            return false;
        }
    }
    const LONG w = bounds.right - bounds.left;
    const LONG h = bounds.bottom - bounds.top;
    if (w <= 0 || h <= 0) {
        return true;
    }
    const std::size_t pixelCount = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    const std::size_t channels = texType == DWRITE_TEXTURE_CLEARTYPE_3x1 ? 3u : 1u;
    state.rawScratch.resize(pixelCount * channels);
    if (FAILED(analysis->CreateAlphaTexture(
            texType, &bounds, state.rawScratch.data(), static_cast<UINT32>(state.rawScratch.size())))) {
        return false;
    }
    state.alphaScratch.resize(pixelCount);
    if (channels == 3u) {
        for (std::size_t i = 0; i < pixelCount; ++i) {
            const BYTE* px = &state.rawScratch[i * 3u];
            const int avg = (px[0] + px[1] + px[2] + 1) / 3;
            state.alphaScratch[i] = state.contrastLut[static_cast<std::size_t>(avg)];
        }
    } else if (useGrayscale) {
        for (std::size_t i = 0; i < pixelCount; ++i) {
            state.alphaScratch[i] = state.contrastLut[state.rawScratch[i]];
        }
    } else {
        for (std::size_t i = 0; i < pixelCount; ++i) {
            state.alphaScratch[i] = state.rawScratch[i] > 0 ? 255 : 0;
        }
    }
    return true;
}

bool DWriteLoaderInit(ImFontAtlas* atlas) {
    if (!atlas) {
        return false;
    }
    if (atlas->FontLoaderData) {
        return true;
    }
    auto state = std::make_unique<DWriteAtlasState>();
    InitContrastLut(state->contrastLut);
    state->faceBuilder = CreateDWriteFaceBuilder();
    if (!state->faceBuilder) {
        return false;
    }
    if (FAILED(::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                     __uuidof(IDWriteFactory),
                                     reinterpret_cast<IUnknown**>(&state->factory)))) {
        DestroyDWriteFaceBuilder(state->faceBuilder);
        return false;
    }
    (void)state->factory->QueryInterface(__uuidof(IDWriteFactory2),
                                         reinterpret_cast<void**>(&state->factory2));
    atlas->FontLoaderData = state.release();
    return true;
}

void DWriteLoaderShutdown(ImFontAtlas* atlas) {
    auto* state = static_cast<DWriteAtlasState*>(atlas ? atlas->FontLoaderData : nullptr);
    if (!state) {
        return;
    }
    if (state->factory2) {
        state->factory2->Release();
        state->factory2 = nullptr;
    }
    if (state->factory) {
        state->factory->Release();
        state->factory = nullptr;
    }
    DestroyDWriteFaceBuilder(state->faceBuilder);
    state->faceBuilder = nullptr;
    delete state;
    atlas->FontLoaderData = nullptr;
}

bool DWriteFontSrcInit(ImFontAtlas* atlas, ImFontConfig* src) {
    if (!atlas || !src || !src->FontData || src->FontDataSize <= 0) {
        return false;
    }
    if (!atlas->FontLoaderData && !DWriteLoaderInit(atlas)) {
        return false;
    }
    auto* state = static_cast<DWriteAtlasState*>(atlas->FontLoaderData);
    void* rawFace = nullptr;
    if (src->FontDataSize > 11 && std::memcmp(src->FontData, "MOTRIX_SYS:", 11) == 0) {
        const std::string_view spec(static_cast<const char*>(src->FontData),
                                    static_cast<std::size_t>(src->FontDataSize));
        rawFace = CreateDWriteSystemFontFace(state->faceBuilder, spec);
    } else {
        rawFace = CreateDWriteMemoryFontFace(state->faceBuilder,
                                             src->FontData,
                                             static_cast<std::uint64_t>(src->FontDataSize),
                                             static_cast<std::uint32_t>(src->FontNo));
    }
    ComRef<IDWriteFontFace> face;
    face.Adopt(static_cast<IDWriteFontFace*>(rawFace));
    if (!face) {
        return false;
    }

    auto srcData = std::make_unique<DWriteFontSrcData>();
    face->GetMetrics(&srcData->metrics);
    const UINT32 totalUnits =
        static_cast<UINT32>(srcData->metrics.ascent) + static_cast<UINT32>(srcData->metrics.descent);
    if (totalUnits == 0 || srcData->metrics.designUnitsPerEm == 0) {
        return false;
    }

    const float rawCap = srcData->metrics.capHeight > 0
                             ? static_cast<float>(srcData->metrics.capHeight)
                             : static_cast<float>(srcData->metrics.ascent) * 0.72f;
    const float rawCapRatio = rawCap / static_cast<float>(totalUnits);
    float visualNorm = 1.0f;
    if (rawCapRatio > 0.25f && rawCapRatio < 1.2f) {
        visualNorm = std::clamp(0.665f / rawCapRatio, 0.95f, 1.24f);
    }
    srcData->scalePerUnit = visualNorm / static_cast<float>(totalUnits);
    srcData->capRatio = rawCapRatio * visualNorm;
    srcData->glyphCount = face->GetGlyphCount();
    srcData->face = face.Detach();
    src->FontLoaderData = srcData.release();

    if (src->MergeMode && src->SizePixels == 0.0f && src->DstFont && !src->DstFont->Sources.empty()) {
        src->SizePixels = src->DstFont->Sources[0]->SizePixels;
    }
    return true;
}

void DWriteFontSrcDestroy(ImFontAtlas* /*atlas*/, ImFontConfig* src) {
    auto* srcData = static_cast<DWriteFontSrcData*>(src ? src->FontLoaderData : nullptr);
    if (!srcData) {
        return;
    }
    if (srcData->face) {
        srcData->face->Release();
    }
    delete srcData;
    src->FontLoaderData = nullptr;
}

int DWriteGetGlyphIndex(ImFontAtlas* /*atlas*/, ImFontConfig* src, ImWchar codepoint) {
    const auto* srcData = static_cast<const DWriteFontSrcData*>(src ? src->FontLoaderData : nullptr);
    const UINT32 cp = static_cast<UINT32>(codepoint);
    UINT16 gid = 0;
    if (!srcData || !srcData->face || FAILED(srcData->face->GetGlyphIndices(&cp, 1, &gid))) {
        return 0;
    }
    return static_cast<int>(gid);
}

bool DWriteContainsGlyph(ImFontAtlas* /*atlas*/, ImFontConfig* src, int glyphIndex) {
    const auto* srcData = static_cast<const DWriteFontSrcData*>(src ? src->FontLoaderData : nullptr);
    return srcData && glyphIndex > 0 && glyphIndex < static_cast<int>(srcData->glyphCount);
}

bool DWriteFontBakedInit(ImFontAtlas* /*atlas*/,
                         ImFontConfig* src,
                         ImFontBaked* baked,
                         void* /*loaderData*/) {
    if (!src || !baked || !src->FontLoaderData || !baked->OwnerFont
        || baked->OwnerFont->Sources.empty()) {
        return false;
    }
    if (src->MergeMode) {
        return true;
    }
    const auto* srcData = static_cast<const DWriteFontSrcData*>(src->FontLoaderData);
    const float density = src->RasterizerDensity * baked->RasterizerDensity;
    if (density <= 0.0f) {
        return false;
    }
    const float size = ComputeEffectiveFontSize(src, baked);
    const float capPx = size * srcData->capRatio;
    const float rawAscentPx = static_cast<float>(srcData->metrics.ascent) * srcData->scalePerUnit * size;
    const float centeredAscent = srcData->capRatio > 0.0f ? (size + capPx) * 0.5f : rawAscentPx;
    baked->Ascent = std::round(centeredAscent * density) / density;
    baked->Descent = std::round((centeredAscent - size) * density) / density;
    return true;
}

[[nodiscard]] bool ComputeRasterMetrics(const DWriteFontSrcData& srcData,
                                        const ImFontConfig* src,
                                        const ImFontBaked* baked,
                                        UINT16 gid,
                                        RasterMetrics& out) noexcept {
    out.rasterizerDensity = src->RasterizerDensity * baked->RasterizerDensity;
    if (out.rasterizerDensity <= 0.0f) {
        return false;
    }
    const float size = ComputeEffectiveFontSize(src, baked);
    const float unitToPx = size * out.rasterizerDensity * srcData.scalePerUnit;
    out.fontEmSize = unitToPx * static_cast<float>(srcData.metrics.designUnitsPerEm);

    DWRITE_GLYPH_METRICS gm{};
    if (FAILED(srcData.face->GetDesignGlyphMetrics(&gid, 1, &gm, FALSE))) {
        return false;
    }
    out.advanceX = static_cast<float>(gm.advanceWidth) * unitToPx / out.rasterizerDensity;
    return true;
}

struct GlyphLoadArgs {
    ImFontAtlas* atlas = nullptr;
    ImFontConfig* src = nullptr;
    ImFontBaked* baked = nullptr;
    int glyphIndex = 0;
    ImFontGlyph* outGlyph = nullptr;
    float* outAdvanceX = nullptr;
};

bool LoadDWriteGlyphImpl(const GlyphLoadArgs& args) {
    ImFontAtlas* atlas = args.atlas;
    ImFontConfig* src = args.src;
    ImFontBaked* baked = args.baked;
    ImFontGlyph* outGlyph = args.outGlyph;
    float* outAdvanceX = args.outAdvanceX;
    auto* state = static_cast<DWriteAtlasState*>(atlas ? atlas->FontLoaderData : nullptr);
    const auto* srcData = static_cast<const DWriteFontSrcData*>(src ? src->FontLoaderData : nullptr);
    if (!state || !srcData || !srcData->face || !baked || args.glyphIndex < 0) {
        return false;
    }
    const UINT16 gid = static_cast<UINT16>(args.glyphIndex);
    RasterMetrics rm{};
    if (!ComputeRasterMetrics(*srcData, src, baked, gid, rm)) {
        return false;
    }
    if (outAdvanceX) {
        *outAdvanceX = rm.advanceX;
        return true;
    }
    if (!outGlyph) {
        return false;
    }

    outGlyph->AdvanceX = rm.advanceX;
    outGlyph->Visible = false;
    outGlyph->Colored = false;
    ComRef<IDWriteGlyphRunAnalysis> analysis;
    RECT bounds{};
    DWRITE_TEXTURE_TYPE texType = DWRITE_TEXTURE_CLEARTYPE_3x1;
    if (!CreateGlyphAnalysis(*state, srcData->face, gid, rm.fontEmSize, analysis)
        || !RasterizeAnalysisAlpha(analysis.Get(), *state, bounds, texType)) {
        return false;
    }
    const int w = static_cast<int>(bounds.right - bounds.left);
    const int h = static_cast<int>(bounds.bottom - bounds.top);
    if (w <= 0 || h <= 0) {
        return true;
    }

    const ImFontAtlasRectId packId = ImFontAtlasPackAddRect(atlas, w, h);
    if (packId == ImFontAtlasRectId_Invalid) {
        return false;
    }
    ImTextureRect* rect = ImFontAtlasPackGetRect(atlas, packId);
    const float refSize = baked->OwnerFont->Sources[0]->SizePixels;
    const float offsetsScale = refSize != 0.0f ? baked->Size / refSize : 1.0f;
    const float fontOffX = ImFloor(src->GlyphOffset.x * offsetsScale + 0.5f);
    const float fontOffY = ImFloor(src->GlyphOffset.y * offsetsScale + 0.5f) + baked->Ascent;
    const float recip = 1.0f / rm.rasterizerDensity;

    outGlyph->X0 = static_cast<float>(bounds.left) * recip + fontOffX;
    outGlyph->Y0 = static_cast<float>(bounds.top) * recip + fontOffY;
    outGlyph->X1 = static_cast<float>(bounds.left + w) * recip + fontOffX;
    outGlyph->Y1 = static_cast<float>(bounds.top + h) * recip + fontOffY;
    outGlyph->Visible = true;
    outGlyph->Colored = false;
    outGlyph->PackId = packId;
    ImFontAtlasBakedSetFontGlyphBitmap(
        atlas, baked, src, outGlyph, rect, state->alphaScratch.data(), ImTextureFormat_Alpha8, w);
    return true;
}

[[nodiscard]] ImFontLoader MakeDWriteLoader() noexcept {
    ImFontLoader loader;
    loader.Name = "DirectWrite";
    loader.LoaderInit = DWriteLoaderInit;
    loader.LoaderShutdown = DWriteLoaderShutdown;
    loader.FontSrcInit = DWriteFontSrcInit;
    loader.FontSrcDestroy = DWriteFontSrcDestroy;
    loader.FontSrcGetGlyphIndexFromCodepoint = DWriteGetGlyphIndex;
    loader.FontSrcContainsGlyph = DWriteContainsGlyph;
    loader.FontBakedInit = DWriteFontBakedInit;
    loader.FontBakedLoadGlyph = [](ImFontAtlas* atlas,
                                   ImFontConfig* src,
                                   ImFontBaked* baked,
                                   void* /*loaderData*/,
                                   int glyphIndex,
                                   ImFontGlyph* outGlyph,
                                   float* outAdvanceX) -> bool {
        return LoadDWriteGlyphImpl({atlas, src, baked, glyphIndex, outGlyph, outAdvanceX});
    };
    return loader;
}

}  // namespace

const ImFontLoader* GetDWriteFontLoader() noexcept {
    static const ImFontLoader kLoader = MakeDWriteLoader();
    return &kLoader;
}

bool HasDWriteSystemFontFamily(std::string_view familyUtf8) {
    const std::string_view trimmed = motrix::core::TrimAscii(familyUtf8);
    if (trimmed.empty()) {
        return false;
    }
    IDWriteFactory* factory = nullptr;
    if (FAILED(::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                     __uuidof(IDWriteFactory),
                                     reinterpret_cast<IUnknown**>(&factory)))) {
        return false;
    }
    IDWriteFontCollection* sysFonts = nullptr;
    const HRESULT colHr = factory->GetSystemFontCollection(&sysFonts, FALSE);
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (SUCCEEDED(colHr) && sysFonts) {
        const std::wstring wideName = motrix::core::PathFromUtf8(trimmed).wstring();
        (void)sysFonts->FindFamilyName(wideName.c_str(), &index, &exists);
    }
    SafeRelease(sysFonts);
    SafeRelease(factory);
    return exists == TRUE;
}

}  // namespace motrix::ui
