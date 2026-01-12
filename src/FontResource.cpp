module FontResource;

void FontAtlasData::SetMetrics(uint32_t unicodeCodepoint, const GlyphMetrics &metrics) {
    EnsureCapacityForCodepoint(unicodeCodepoint);
    Glyphs[(unicodeCodepoint >> 24) & 0xFF]
            [(unicodeCodepoint >> 16) & 0xFF]
            [(unicodeCodepoint >> 8) & 0xFF]
            [unicodeCodepoint & 0xFF] = metrics;
}

const GlyphMetrics &FontAtlasData::ReadMetrics(uint32_t unicodeCodepoint) const {
    return *Glyphs[(unicodeCodepoint >> 24) & 0xFF]
            [(unicodeCodepoint >> 16) & 0xFF]
            [(unicodeCodepoint >> 8) & 0xFF]
            [unicodeCodepoint & 0xFF];
}

void FontAtlasData::EnsureCapacityForCodepoint(uint32_t cp) {
    const size_t i0 = (cp >> 24) & 0xFF;
    const size_t i1 = (cp >> 16) & 0xFF;
    const size_t i2 = (cp >> 8) & 0xFF;
    const size_t i3 = cp & 0xFF;

    if (Glyphs.size() <= i0) Glyphs.resize(i0 + 1);

    auto &level1 = Glyphs[i0];
    if (level1.size() <= i1) level1.resize(i1 + 1);

    auto &level2 = level1[i1];
    if (level2.size() <= i2) level2.resize(i2 + 1);

    auto &level3 = level2[i2];
    if (level3.size() <= i3) level3.resize(i3 + 1);
}

const GlyphMetrics *FontAtlasData::ReadMetricsSafe(uint32_t cp) const {
    const size_t i0 = (cp >> 24) & 0xFF;
    const size_t i1 = (cp >> 16) & 0xFF;
    const size_t i2 = (cp >> 8) & 0xFF;
    const size_t i3 = cp & 0xFF;

    if (i0 >= Glyphs.size()) return nullptr;
    const auto &l1 = Glyphs[i0];

    if (i1 >= l1.size()) return nullptr;
    const auto &l2 = l1[i1];

    if (i2 >= l2.size()) return nullptr;
    const auto &l3 = l2[i2];

    if (i3 >= l3.size()) return nullptr;
    return &*l3[i3];
}

std::unique_ptr<FontAtlasData> GenerateFontAtlas(const GenerateFontAtlasInfo& info) {
    using namespace msdf_atlas;

    // 1. Gather glyphs from all fonts and charsets
    std::vector<GlyphGeometry> allGlyphs;
    for (const auto& fontEntry : info.FontsToBake) {
        msdfgen::FontHandle* fontHandle = fontEntry.first;
        FontGeometry fontGeometry(&allGlyphs);

        for (const auto& charset : fontEntry.second) {
            fontGeometry.loadCharset(fontHandle, 1.0, charset);
        }
    }

    // 2. Apply MSDF edge coloring
    for (GlyphGeometry& glyph : allGlyphs) {
        glyph.edgeColoring(&msdfgen::edgeColoringInkTrap, 3.0, 0);
    }

    // 3. Setup packer and compute layout
    TightAtlasPacker packer;
    packer.setDimensionsConstraint(info.DimensionsConstraint);
    packer.setMinimumScale(info.MinimumScale);
    packer.setPixelRange(info.PixelRange.lower);
    packer.setMiterLimit(info.MiterLimit);
    packer.pack(allGlyphs.data(), static_cast<int>(allGlyphs.size()));

    int width = 0, height = 0;
    packer.getDimensions(width, height);

    // 4. Generate MSDF pixels (RGB)
    ImmediateAtlasGenerator<float, 3, msdfGenerator, BitmapAtlasStorage<uint8_t, 3>> generator(width, height);
    GeneratorAttributes attributes;
    generator.setAttributes(attributes);
    generator.generate(allGlyphs.data(), static_cast<int>(allGlyphs.size()));

    // 5. Initialize result data
    auto result = std::make_unique<FontAtlasData>();
    result->AtlasWidth = static_cast<uint32_t>(width);
    result->AtlasHeight = static_cast<uint32_t>(height);
    result->MSDFPixelRange = static_cast<float>(info.PixelRange.lower);

    // 6. Access storage using your specified method
    // This triggers the conversion or copy based on your BitmapAtlasStorage implementation
    BitmapAtlasStorage<unsigned char, 3> atlasRef = generator.atlasStorage();

    // Cast to BitmapRef to access raw pixels via (x, y) coordinates
    msdfgen::BitmapRef<unsigned char, 3> bitmapPtr = static_cast<msdfgen::BitmapRef<unsigned char, 3>>(atlasRef);

    // 7. Convert RGB to RGBA for NVRHI compatibility
    size_t pixelCount = static_cast<size_t>(width) * height;
    result->AtlasBitmapData = std::make_unique<uint8_t[]>(pixelCount * 4);
    uint8_t* rgbaBuffer = result->AtlasBitmapData.get();

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const unsigned char* rgb = bitmapPtr(x, y);
            size_t targetIndex = (static_cast<size_t>(y) * width + x) * 4;

            rgbaBuffer[targetIndex + 0] = rgb[0]; // R
            rgbaBuffer[targetIndex + 1] = rgb[1]; // G
            rgbaBuffer[targetIndex + 2] = rgb[2]; // B
            rgbaBuffer[targetIndex + 3] = 255;    // A (Solid)
        }
    }

    // 8. Populate Glyph Metrics in the 4-level paging system
    for (const auto& glyph : allGlyphs) {
        GlyphMetrics metrics;

        // Compute UVs (Normalized coordinates for Shader)
        double al, ab, ar, at;
        glyph.getQuadAtlasBounds(al, ab, ar, at);
        metrics.BottomLeftUV = glm::vec2(al / width, ab / height);
        metrics.TopRightUV   = glm::vec2(ar / width, at / height);

        // Compute Quad Geometry (Relative to Font Size)
        double pl, pb, pr, pt;
        glyph.getQuadPlaneBounds(pl, pb, pr, pt);
        metrics.Size   = glm::vec2(pr - pl, pt - pb);
        metrics.Offset = glm::vec2(pl, pb);
        metrics.Advance = static_cast<float>(glyph.getAdvance());

        result->SetMetrics(glyph.getCodepoint(), metrics);
    }

    return result;
}