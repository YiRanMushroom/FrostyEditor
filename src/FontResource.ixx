export module FontResource;

export import "msdf-atlas-gen/msdf-atlas-gen/msdf-atlas-gen.h";
import Core.Prelude;

export struct GlyphMetrics {
    glm::vec2 BottomLeftUV;
    glm::vec2 TopRightUV;
    glm::vec2 Size;
    glm::vec2 Offset;
    float Advance;
};

// Virtual address system, if we want to support UTF 8, we need to have 4 - levels of paging and addressing, so
// it is std::vector<std::vector<std::vector<std::vector<GlyphMetrics>>>>
export struct FontAtlasData {
    std::vector<std::vector<std::vector<std::vector<std::optional<GlyphMetrics>>>>> Glyphs;
    std::unique_ptr<uint32_t[]> AtlasBitmapData; // Always RGBA 8-bit
    uint32_t PixelCount = 0;
    uint32_t AtlasWidth = 0;
    uint32_t AtlasHeight = 0;
    float MSDFPixelRange = 4.0f;

    void SetMetrics(uint32_t unicodeCodepoint, const GlyphMetrics& metrics);
    [[nodiscard]] const GlyphMetrics& ReadMetrics(uint32_t unicodeCodepoint) const;
    [[nodiscard]] const GlyphMetrics* ReadMetricsSafe(uint32_t unicodeCodepoint) const;

private:
    void EnsureCapacityForCodepoint(uint32_t unicodeCodepoint);
};

export struct GenerateFontAtlasInfo {
    std::vector<std::pair<msdfgen::FontHandle*, std::vector<msdf_atlas::Charset>>> FontsToBake;
    double MinimumScale = 32.0;
    double PixelRange = 4.0;
    double MiterLimit = 1.0;
    msdf_atlas::DimensionsConstraint DimensionsConstraint = msdf_atlas::DimensionsConstraint::POWER_OF_TWO_SQUARE;
};

export std::unique_ptr<FontAtlasData> GenerateFontAtlas(const GenerateFontAtlasInfo& info);