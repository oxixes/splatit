#ifndef SPLATOON_SERVER_FTEX_HPP
#define SPLATOON_SERVER_FTEX_HPP

#include "bfres.hpp"

namespace boss::bfres {

    enum class Dimension {
        SURFACE_DIM_1D            = 0x000,
        SURFACE_DIM_2D            = 0x001,
        SURFACE_DIM_3D            = 0x002,
        SURFACE_DIM_CUBE          = 0x003,
        SURFACE_DIM_1D_ARRAY      = 0x004,
        SURFACE_DIM_2D_ARRAY      = 0x005,
        SURFACE_DIM_2D_MSAA       = 0x006,
        SURFACE_DIM_2D_MSAA_ARRAY = 0x007,
        SURFACE_DIM_FIRST         = SURFACE_DIM_1D,
        SURFACE_DIM_LAST          = SURFACE_DIM_2D_MSAA_ARRAY
    };

    enum class TileMode {
        M_DEFAULT        = 0x00000000,
        M_LINEAR_SPECIAL = 0x00000010,
        M_LINEAR_ALIGNED = 0x00000001,
        M_1D_TILED_THIN1 = 0x00000002,
        M_1D_TILED_THICK = 0x00000003,
        M_2D_TILED_THIN1 = 0x00000004,
        M_2D_TILED_THIN2 = 0x00000005,
        M_2D_TILED_THIN4 = 0x00000006,
        M_2D_TILED_THICK = 0x00000007,
        M_2B_TILED_THIN1 = 0x00000008,
        M_2B_TILED_THIN2 = 0x00000009,
        M_2B_TILED_THIN4 = 0x0000000a,
        M_2B_TILED_THICK = 0x0000000b,
        M_3D_TILED_THIN1 = 0x0000000c,
        M_3D_TILED_THICK = 0x0000000d,
        M_3B_TILED_THIN1 = 0x0000000e,
        M_3B_TILED_THICK = 0x0000000f,
    };

    enum class AAMode {
        M_1X    = 0x000,
        M_2X    = 0x001,
        M_4X    = 0x002,
        M_8X    = 0x003,
        M_FIRST = M_1X,
        M_LAST  = M_8X
    };

    enum class Usage {
        SURFACE_USE_TEXTURE                  = 0x001,
        SURFACE_USE_COLOR_BUFFER             = 0x002,
        SURFACE_USE_DEPTH_BUFFER             = 0x004,

        SURFACE_USE_SCAN_BUFFER              = 0x008,
        SURFACE_USE_FTV                      = (1<<31),

        SURFACE_USE_COLOR_BUFFER_TEXTURE     = SURFACE_USE_COLOR_BUFFER | SURFACE_USE_TEXTURE,
        SURFACE_USE_DEPTH_BUFFER_TEXTURE     = SURFACE_USE_DEPTH_BUFFER | SURFACE_USE_TEXTURE,

        SURFACE_USE_COLOR_BUFFER_FTV         = SURFACE_USE_COLOR_BUFFER | SURFACE_USE_FTV,
        SURFACE_USE_COLOR_BUFFER_TEXTURE_FTV = SURFACE_USE_COLOR_BUFFER_TEXTURE | SURFACE_USE_FTV,

        SURFACE_USE_FIRST                    = SURFACE_USE_TEXTURE,
        SURFACE_USE_LAST                     = SURFACE_USE_SCAN_BUFFER
    };

    struct EncoderResult {
        std::vector<uint8_t> data;
        std::vector<std::vector<uint8_t>> mipmapData;
        std::vector<uint32_t> alignments;
        std::vector<uint32_t> sizes;
        uint32_t format;
        TileMode tileMode;
        AAMode aaMode;
        Dimension dimension;
        uint32_t swizzle;
        uint32_t pitch;
        uint32_t bpp;
        uint32_t depth;
        int width;
        int height;
        int mipmapCount;
    };

    class ImageEncoder {
    public:
        ImageEncoder() = default;
        virtual ~ImageEncoder() = default;

        virtual EncoderResult encode(uint8_t* imgData, int width, int height, int mipmaps) = 0;
    };

    struct SwizzleResult {
        std::vector<uint8_t> data;
        uint32_t pitch;
        uint32_t size;
        uint32_t alignment;
    };

    /*
     * Currently ONLY supports tile mode M_2D_TILED_THIN1 and AA mode M_1X
     */
    class BC1_GTX_Encoder : public ImageEncoder {
    public:
        BC1_GTX_Encoder(TileMode tileMode, AAMode aaMode) : tileMode(tileMode), aaMode(aaMode) {};
        ~BC1_GTX_Encoder() override = default;

        EncoderResult encode(uint8_t* imgData, int width, int height, int mipmaps) override;

    private:
        static SwizzleResult swizzleTexture(std::vector<uint8_t>& compressedData, int unadjustedWidth,
                                            int unadjustedHeight, int width, int height, TileMode tileMode,
                                            AAMode aaMode, uint32_t swizzle, int mipLevel);

        TileMode tileMode;
        AAMode aaMode;

        constexpr static uint32_t FORMAT = 0x431; // SURFACE_FORMAT_T_BC1_SRGB
        constexpr static uint32_t BITS_PER_PIXEL = 64; // 64 bits per block (8 bytes)
    };

    class FTEX : public Subfile {
    public:
        FTEX(const std::vector<uint8_t>& fileData, std::string fileName, const std::shared_ptr<ImageEncoder>& encoder,
             int mipmapCount, Usage usage, int desiredWidth = -1, int desiredHeight = -1);
        ~FTEX() override = default;

        std::pair<std::vector<uint8_t>, std::vector<uint8_t>> serialize(uint32_t dataOffset,
                                        uint32_t extraDataOffset,
                                        const std::map<std::string, uint32_t, StringTableComparator>& stringTableOffsets) override;

        std::set<std::string> getStrings() override;
        uint32_t getDataLength() override;
        uint32_t getAlignment() override;
        SubfileType getType() override;

    private:
        void generateHeader(const EncoderResult& encodedData);
        void generateData(const EncoderResult& encodedData);

        std::vector<uint8_t> data;
        std::vector<uint8_t> extraData;
        uint32_t dataLength;
        uint32_t dataAlignment;
        uint32_t alignment = -1;
        Usage usage;
        std::string fileName;
    };

} // namespace boss::bfres

#endif //SPLATOON_SERVER_FTEX_HPP