#include "ftex.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_DXT_IMPLEMENTATION
#include <stb_dxt.h>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

#include <fstream>
#include <cassert>

#include "../../util/util.hpp"

namespace boss::bfres {

// The following code is not efficient, since it's just meant to be run by the admin
// on very rare occasions, so it's good enough for now.

// BC1 compression in GTX format is used for Splatoon textures.
EncoderResult BC1_GTX_Encoder::encode(uint8_t* imgData, int width, int height, int mipmaps) {
    std::vector<uint8_t> data;
    std::vector<std::vector<uint8_t>> mipmapsData(mipmaps - 1, std::vector<uint8_t>());
    std::vector<uint32_t> alignments(mipmaps, 0);
    std::vector<uint32_t> sizes(mipmaps, 0);

    uint32_t swizzleValue = 0xd0000;
    uint32_t pitch = 0;

    uint8_t* originalImgData = imgData;
    int originalWidth = width;
    int originalHeight = height;
    for (int i = 0; i < mipmaps; i++) {
        bool original = i == 0;
        uint8_t* newImgData = imgData;

        int unadjustedWidth = originalWidth >> i; if (unadjustedWidth == 0) unadjustedWidth = 1;
        int unadjustedHeight = originalHeight >> i; if (unadjustedHeight == 0) unadjustedHeight = 1;
        width = unadjustedWidth;
        height = unadjustedHeight;

        if (!original) {
            // Resize the image for the next mipmap level
            newImgData = new uint8_t[width * height * 4];
            stbir_resize_uint8_srgb(originalImgData, originalWidth, originalHeight, 0, newImgData, width, height, 0, STBIR_RGBA);
        }

        int numBlocksX = (width + 3) / 4;
        int numBlocksY = (height + 3) / 4;

        int numBlocks = numBlocksX * numBlocksY;
        if (numBlocks == 0) numBlocks = 1;
        std::vector<uint8_t> compressedData(numBlocks * 8, 0); // 8 bytes for a single block
        for (int y = 0; y < height; y += 4) {
            for (int x = 0; x < width; x += 4) {
                uint8_t block[4 * 4 * 4] = {0}; // Initialize all elements to 0 for padding
                for (int by = 0; by < 4 && y + by < height; by++) {
                    for (int bx = 0; bx < 4 && x + bx < width; bx++) {
                        uint8_t* pixel = newImgData + (((y + by) * width + (x + bx)) * 4);
                        std::copy(pixel, pixel + 4, block + ((by * 4 + bx) * 4));
                    }
                }
                stb_compress_dxt_block(&compressedData[((y / 4) * numBlocksX + x / 4) * 8], block, 0, STB_DXT_HIGHQUAL);
            }
        }

        auto swizzleResults = swizzleTexture(compressedData, unadjustedWidth, unadjustedHeight, width, height,
                                             tileMode, aaMode, swizzleValue, i);
        if (original) pitch = swizzleResults.pitch;
        alignments[i] = swizzleResults.alignment;
        sizes[i] = swizzleResults.size;

        if (!original) delete[] newImgData;

        if (original) data = swizzleResults.data;
        else mipmapsData[i - 1] = swizzleResults.data;
    }

    return {data, mipmapsData, alignments, sizes, FORMAT, tileMode, aaMode, Dimension::SURFACE_DIM_2D, swizzleValue,
            pitch, BITS_PER_PIXEL / 8, 1, originalWidth, originalHeight, mipmaps};
}

// This function has been based off this code: https://github.com/aboood40091/BFRES-Tool/blob/master/addrlib/addrlib.py
SwizzleResult BC1_GTX_Encoder::swizzleTexture(std::vector<uint8_t>& compressedData,
                                              int unadjustedWidth, int unadjustedHeight,
                                              int width, int height, TileMode tileMode, AAMode aaMode,
                                              uint32_t swizzle, int mipLevel) {
    if (tileMode != TileMode::M_2D_TILED_THIN1) {
        throw std::runtime_error("Only M_2D_TILED_THIN1 tile mode is supported for BC1_GTX_Encoder currently.");
    }

    if (aaMode != AAMode::M_1X) {
        throw std::runtime_error("Only M_1X AA mode is supported for BC1_GTX_Encoder currently.");
    }

    uint32_t macroTileAspectRatio = 1; // 1 for 2D_TILED_THIN1
    uint32_t widthAlignFactor = 1; // 1 for this format
    uint32_t macroTileWidth = 32 / macroTileAspectRatio;
    uint32_t macroTileHeight = 16 * macroTileAspectRatio;
    uint32_t thickness = 1; // 1 for 2D_TILED_THIN1
    uint32_t numSamples = 1 << static_cast<uint32_t>(aaMode);

    uint32_t blockWidth = ((mipLevel) ? util::getNextPowerOfTwo(unadjustedWidth) : unadjustedWidth) / 4;
    uint32_t blockHeight = ((mipLevel) ? util::getNextPowerOfTwo(unadjustedHeight) : unadjustedHeight) / 4;

    if (blockWidth == 0) blockWidth = 1;
    if (blockHeight == 0) blockHeight = 1;

    // We get the next power of 2 for the width and height
    uint32_t alignedWidth = util::getNextPowerOfTwo(blockWidth);
    uint32_t alignedHeight = util::getNextPowerOfTwo(blockHeight);

    if (mipLevel && (alignedWidth < widthAlignFactor * macroTileWidth || alignedHeight < macroTileHeight)) {
        tileMode = TileMode::M_1D_TILED_THIN1;
    }

    uint32_t heightAlign = 0;
    uint32_t pitchAlign = 0;
    uint32_t baseAlign = 0;
    if (tileMode == TileMode::M_2D_TILED_THIN1) {
        uint32_t macroTileBytes = numSamples * ((BITS_PER_PIXEL * macroTileHeight * macroTileWidth + 7) >> 3);

        heightAlign = macroTileAspectRatio * 16;
        pitchAlign = macroTileWidth * (256 / BITS_PER_PIXEL / (8 * thickness) / numSamples);
        if (pitchAlign < macroTileWidth) pitchAlign = macroTileWidth;

        baseAlign = (numSamples * heightAlign * BITS_PER_PIXEL * pitchAlign + 7) >> 3;
        if (baseAlign > macroTileBytes) baseAlign = macroTileBytes;
    } else if (tileMode == TileMode::M_1D_TILED_THIN1) {
        heightAlign = 8;
        baseAlign = 256;
        pitchAlign = 256 / BITS_PER_PIXEL / numSamples / thickness;
        if (pitchAlign < 8) pitchAlign = 8;
    }

    uint32_t surfHeight = util::pow2Align(blockHeight, heightAlign);
    uint32_t surfPitch = 0;

    if (!(pitchAlign & (pitchAlign - 1))) {
        surfPitch = util::pow2Align(blockWidth, pitchAlign);
    } else {
        surfPitch = ((pitchAlign + blockWidth - 1) / pitchAlign) * pitchAlign;
    }

    uint32_t numSlices = 1;
    uint32_t surfSize = (surfHeight * surfPitch * numSlices * BITS_PER_PIXEL * numSamples + 7) / 8;

    assert(surfSize >= compressedData.size() && "Compressed data size is larger than the surface size.");

    std::vector<uint8_t> swizzledData(surfSize, 0);
    compressedData.resize(surfSize, 0); // Resize the compressed data to the surface size

    uint32_t numBlockX = (width + 3) / 4; if (numBlockX == 0) numBlockX = 1;
    uint32_t numBlockY = (height + 3) / 4; if (numBlockY == 0) numBlockY = 1;
    uint32_t pipeSwizzle = (swizzle >> 8) & 1;
    uint32_t bankSwizzle = (swizzle >> 9) & 3;

    uint32_t bytesPerPixel = BITS_PER_PIXEL / 8;

    for (uint32_t y = 0; y < numBlockY; y++) {
        for (uint32_t x = 0; x < numBlockX; x++) {
            uint32_t resultPos = 0;
            if (tileMode == TileMode::M_1D_TILED_THIN1) {
                uint32_t microTileThickness = 1;
                uint32_t microTileBytes = (64 * microTileThickness * BITS_PER_PIXEL + 7) / 8;
                uint32_t microTilesPerRow = surfPitch >> 3;
                uint32_t microTileIndexX = x >> 3;
                uint32_t microTileIndexY = y >> 3;

                uint32_t microTileOffset = microTileBytes * (microTileIndexX + microTileIndexY * microTilesPerRow);
                uint32_t pixelIndex = (32 * ((y & 4) >> 2) | 16 * ((y & 2) >> 1) | 8 * ((x & 4) >> 2) |
                                       4 * ((x & 2) >> 1) | 2 * (y & 1) | x & 1);
                uint32_t pixelOffset = (BITS_PER_PIXEL * pixelIndex) >> 3;

                resultPos = pixelOffset + microTileOffset;
            } else {
                uint32_t microTileThickness = thickness;
                uint32_t pixelIndex = (32 * ((y & 4) >> 2) | 16 * ((y & 2) >> 1) | 8 * ((x & 4) >> 2) |
                                       4 * ((x & 2) >> 1) | 2 * (y & 1) | x & 1);
                uint32_t elemOffset = (BITS_PER_PIXEL * pixelIndex + 7) / 8;
                uint32_t sampleSlice = 0;
                uint32_t pipe = ((y >> 3) ^ (x >> 3)) & 1;
                uint32_t bank = ((y >> 5) ^ (x >> 3)) & 1 | 2 * (((y >> 4) ^ (x >> 4)) & 1);
                uint32_t posSwizzle = pipeSwizzle + 2 * bankSwizzle;
                uint32_t bankPipe = ((pipe + 2 * bank) ^ (6 * sampleSlice ^ posSwizzle)) % 8;
                pipe = bankPipe % 2;
                bank = bankPipe / 2;
                uint32_t sliceBytes = (surfHeight * surfPitch * microTileThickness * BITS_PER_PIXEL * numSamples + 7) / 8;
                uint32_t sliceOffset = sliceBytes * (sampleSlice / microTileThickness);

                uint32_t macroTilePitch = 32;
                uint32_t posMacroTileHeight = 16;

                uint32_t macroTilesPerRow = surfPitch / macroTilePitch;
                uint32_t macroTileBytes = (numSamples * microTileThickness * BITS_PER_PIXEL * posMacroTileHeight * macroTilePitch + 7) / 8;
                uint32_t macroTileIndexX = x / macroTilePitch;
                uint32_t macroTileIndexY = y / posMacroTileHeight;
                uint32_t macroTileOffset = (macroTileIndexX + macroTilesPerRow * macroTileIndexY) * macroTileBytes;

                uint32_t totalOffset = elemOffset + ((macroTileOffset + sliceOffset) >> 3);
                resultPos = bank << 9 | pipe << 8 | 255 & totalOffset | (totalOffset & -256) << 3;
            }

            uint32_t dataPos = (y * numBlockX + x) * bytesPerPixel;
            if (dataPos + bytesPerPixel <= compressedData.size() && resultPos + bytesPerPixel <= compressedData.size()) {
                std::copy(compressedData.begin() + dataPos, compressedData.begin() + dataPos + bytesPerPixel, swizzledData.begin() + resultPos);
            }
        }
    }

    return {swizzledData, surfPitch, surfSize, baseAlign};
}

// FTEX class

FTEX::FTEX(const std::vector<uint8_t>& fileData, std::string fileName, const std::shared_ptr<ImageEncoder>& encoder,
           int mipmapCount, Usage usage, int desiredWidth, int desiredHeight) {
    if (mipmapCount <= 0) {
        throw std::runtime_error("Mipmap count must be more than 0.");
    }

    if (mipmapCount > 14) { // This restriction is due to the FTEX format, which only allows 13 mipmaps + the original image
        throw std::runtime_error("Mipmap count cannot be more than 14.");
    }

    this->usage = usage;
    this->fileName = std::move(fileName);

    int width, height, channels;
    uint8_t* imgData = stbi_load_from_memory(fileData.data(), (int) fileData.size(), &width, &height, &channels, 4);
    if (imgData == nullptr) {
        std::string error = stbi_failure_reason();
        throw std::runtime_error("Failed to load image file: " + error);
    }

    // Resize the image if desiredWidth or desiredHeight are more than 0
    bool resized = false;
    uint8_t* dataToEncode = imgData;
    if (desiredWidth > 0 || desiredHeight > 0) {
        int newWidth = (desiredWidth > 0) ? desiredWidth : width;
        int newHeight = (desiredHeight > 0) ? desiredHeight : height;

        dataToEncode = new uint8_t[newWidth * newHeight * 4];
        stbir_resize_uint8_srgb(imgData, width, height, 0, dataToEncode, newWidth, newHeight, 0, STBIR_RGBA);

        resized = true;
        width = newWidth;
        height = newHeight;
    }

    auto encodedData = encoder->encode(dataToEncode, width, height, mipmapCount);
    generateHeader(encodedData);
    generateData(encodedData);
    alignment = 512 * encodedData.bpp;

    if (resized) delete[] dataToEncode;
    stbi_image_free(imgData);
}

void FTEX::generateHeader(const EncoderResult& encodedData) {
    data = {0x46, 0x54, 0x45, 0x58}; // FTEX

    uint32_t zeroBig = 0; // 0 in big endian, will be used in some places

    auto dimension = static_cast<uint32_t>(encodedData.dimension);
    util::getu32Big(dimension);
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&dimension), reinterpret_cast<uint8_t*>(&dimension) + 4);

    auto width = static_cast<uint32_t>(encodedData.width);
    util::getu32Big(width);
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&width), reinterpret_cast<uint8_t*>(&width) + 4);

    auto height = static_cast<uint32_t>(encodedData.height);
    util::getu32Big(height);
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&height), reinterpret_cast<uint8_t*>(&height) + 4);

    uint32_t depth = encodedData.depth;
    util::getu32Big(depth);
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&depth), reinterpret_cast<uint8_t*>(&depth) + 4);

    uint32_t numMipmaps = encodedData.mipmapCount;
    util::getu32Big(numMipmaps);
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&numMipmaps), reinterpret_cast<uint8_t*>(&numMipmaps) + 4);

    uint32_t format = encodedData.format;
    util::getu32Big(format);
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&format), reinterpret_cast<uint8_t*>(&format) + 4);

    auto aaMode = static_cast<uint32_t>(encodedData.aaMode);
    util::getu32Big(aaMode);
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&aaMode), reinterpret_cast<uint8_t*>(&aaMode) + 4);

    auto usageInt = static_cast<uint32_t>(this->usage);
    util::getu32Big(usageInt);
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&usageInt), reinterpret_cast<uint8_t*>(&usageInt) + 4);

    // Texture data length, will be set in generateData
    uint32_t dataLength = 0;
    util::getu32Big(dataLength);
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&dataLength), reinterpret_cast<uint8_t*>(&dataLength) + 4);

    // Data pointer, set to 0 in files
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&zeroBig), reinterpret_cast<uint8_t*>(&zeroBig) + 4);

    // Mipmap data length, will be set in generateData
    uint32_t mipmapsDataLength = 0;
    util::getu32Big(mipmapsDataLength);
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&mipmapsDataLength), reinterpret_cast<uint8_t*>(&mipmapsDataLength) + 4);

    // Mipmaps pointer, set to 0 in files
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&zeroBig), reinterpret_cast<uint8_t*>(&zeroBig) + 4);

    auto tileMode = static_cast<uint32_t>(encodedData.tileMode);
    util::getu32Big(tileMode);
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&tileMode), reinterpret_cast<uint8_t*>(&tileMode) + 4);

    uint32_t swizzle = encodedData.swizzle;
    util::getu32Big(swizzle);
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&swizzle), reinterpret_cast<uint8_t*>(&swizzle) + 4);

    uint32_t dataAlignment = 512 * encodedData.bpp;
    util::getu32Big(dataAlignment);
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&dataAlignment), reinterpret_cast<uint8_t*>(&dataAlignment) + 4);

    uint32_t pitch = encodedData.pitch;
    util::getu32Big(pitch);
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&pitch), reinterpret_cast<uint8_t*>(&pitch) + 4);

    // Mipmap offsets, will be set in serialize
    for (int i = 0; i < 13; i++) {
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&zeroBig), reinterpret_cast<uint8_t*>(&zeroBig) + 4);
    }

    // First mipmap, will be 0
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&zeroBig), reinterpret_cast<uint8_t*>(&zeroBig) + 4);

    // Number of mipmaps again
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&numMipmaps), reinterpret_cast<uint8_t*>(&numMipmaps) + 4);

    uint32_t firstSlice = 0;
    util::getu32Big(firstSlice);
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&firstSlice), reinterpret_cast<uint8_t*>(&firstSlice) + 4);

    uint32_t numSlices = 1;
    util::getu32Big(numSlices);
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&numSlices), reinterpret_cast<uint8_t*>(&numSlices) + 4);

    // Component selector, will be always the same in this case
    uint8_t componentSelector[] = {0, 1, 2, 3};
    data.insert(data.end(), componentSelector, componentSelector + 4);

    // Texture registers, will be 0
    for (int i = 0; i < 5; i++) {
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&zeroBig), reinterpret_cast<uint8_t*>(&zeroBig) + 4);
    }

    // Texture handler, 0 in files
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&zeroBig), reinterpret_cast<uint8_t*>(&zeroBig) + 4);

    uint32_t arrayLength = 1;
    util::getu32Big(arrayLength);
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&arrayLength), reinterpret_cast<uint8_t*>(&arrayLength) + 4);

    // File name offset, will be set in serialize
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&zeroBig), reinterpret_cast<uint8_t*>(&zeroBig) + 4);

    // File path offset, will be set in serialize
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&zeroBig), reinterpret_cast<uint8_t*>(&zeroBig) + 4);

    // Data offset, will be set in serialize
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&zeroBig), reinterpret_cast<uint8_t*>(&zeroBig) + 4);

    // Mipmap offset, will be set in serialize
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&zeroBig), reinterpret_cast<uint8_t*>(&zeroBig) + 4);

    // User data index group offset, not supported, set to 0
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&zeroBig), reinterpret_cast<uint8_t*>(&zeroBig) + 4);

    // User data entry count + padding, not supported, set to 0
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&zeroBig), reinterpret_cast<uint8_t*>(&zeroBig) + 4);

    assert(data.size() == 192);
}

void FTEX::generateData(const EncoderResult& encodedData) {
    std::vector<uint8_t> mipmapData;
    std::vector<uint32_t> offsets(encodedData.mipmapCount - 1, 0);
    uint32_t currentOffset = 0;
    for (int i = 0; i < encodedData.mipmapCount; i++) {
        if (!i) {
            mipmapData.insert(mipmapData.end(), encodedData.data.begin(), encodedData.data.end());
        } else {
            if (i == 1) {
                offsets[0] = encodedData.data.size();
            } else {
                offsets[i - 1] = currentOffset;
            }

            // Align the data to the surface alignment
            while (currentOffset % encodedData.alignments[i] != 0) {
                mipmapData.push_back(0);
                currentOffset++;
            }

            mipmapData.insert(mipmapData.end(), encodedData.mipmapData[i - 1].begin(), encodedData.mipmapData[i - 1].end());
            currentOffset += encodedData.mipmapData[i - 1].size();
        }
    }

    // We set the data length in the header
    dataLength = encodedData.data.size();
    util::getu32Big(dataLength);
    std::copy(reinterpret_cast<uint8_t*>(&dataLength), reinterpret_cast<uint8_t*>(&dataLength) + 4, data.begin() + 0x24);

    // We set the mipmaps data length in the header
    uint32_t mipmapsDataLength = mipmapData.size() - encodedData.data.size();
    util::getu32Big(mipmapsDataLength);
    std::copy(reinterpret_cast<uint8_t*>(&mipmapsDataLength), reinterpret_cast<uint8_t*>(&mipmapsDataLength) + 4, data.begin() + 0x2C);

    // We set the mipmap offsets in the header
    for (int i = 0; i < encodedData.mipmapCount - 1; i++) {
        util::getu32Big(offsets[i]);
        std::copy(reinterpret_cast<uint8_t*>(&offsets[i]), reinterpret_cast<uint8_t*>(&offsets[i]) + 4, data.begin() + 0x44 + i * 4);
    }

    dataLength = encodedData.data.size();
    dataAlignment = encodedData.alignments[0];
    extraData = mipmapData;
}

std::pair<std::vector<uint8_t>, std::vector<uint8_t>> FTEX::serialize(uint32_t dataOffset,
                                                                uint32_t extraDataOffset,
                                                                const std::map<std::string, uint32_t, StringTableComparator>& stringTableOffsets) {
    // We need to set the file name offset, file path offset, data offset and mipmap offsets
    uint32_t fileNameOffset = stringTableOffsets.at(fileName) - dataOffset - 0xA8;
    util::getu32Big(fileNameOffset);
    std::copy(reinterpret_cast<uint8_t*>(&fileNameOffset), reinterpret_cast<uint8_t*>(&fileNameOffset) + 4, data.begin() + 0xA8);

    uint32_t filePathOffset = stringTableOffsets.at("") - dataOffset - 0xAC;
    util::getu32Big(filePathOffset);
    std::copy(reinterpret_cast<uint8_t*>(&filePathOffset), reinterpret_cast<uint8_t*>(&filePathOffset) + 4, data.begin() + 0xAC);

    uint32_t offset = extraDataOffset;
    // Insert 0 at the beginning of the data to align it to the surface alignment
    while (offset % dataAlignment != 0) {
        extraData.insert(extraData.begin(), 0);
        offset++;
    }

    uint32_t dataOffsetBig = offset - dataOffset - 0xB0;
    uint32_t mipmapOffset = offset + dataLength - dataOffset - 0xB4;
    util::getu32Big(dataOffsetBig);
    std::copy(reinterpret_cast<uint8_t*>(&dataOffsetBig), reinterpret_cast<uint8_t*>(&dataOffsetBig) + 4, data.begin() + 0xB0);

    util::getu32Big(mipmapOffset);
    std::copy(reinterpret_cast<uint8_t*>(&mipmapOffset), reinterpret_cast<uint8_t*>(&mipmapOffset) + 4, data.begin() + 0xB4);

    return std::make_pair(data, extraData);
}

std::set<std::string> FTEX::getStrings() {
    return {fileName, ""};
}

uint32_t FTEX::getDataLength() {
    return data.size(); // Length of the FTEX header
}

uint32_t FTEX::getAlignment() {
    return alignment;
}

SubfileType FTEX::getType() {
    return SubfileType::TEXTURE;
}

} // namespace boss::bfres