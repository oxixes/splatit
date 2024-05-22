#ifndef SPLATOON_SERVER_BFRES_HPP
#define SPLATOON_SERVER_BFRES_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <optional>

namespace boss::bfres {

    enum class SubfileType {
        MODEL,
        TEXTURE,
        SKELETON,
        SHADER,
        COLOR,
        TEXTURE_SRT,
        TEXTURE_PATTERN,
        BONE_VISIBILITY,
        MATERIAL_VISIBILITY,
        SHAPE,
        SCENE,
        EMBEDDED_FILE
    };

    struct StringTableComparator {
        bool operator()(const std::string& a, const std::string& b) const {
            if (a.empty()) return false;
            if (b.empty()) return true;
            return a < b;
        }
    };

    class Subfile {
    public:
        Subfile() = default;
        virtual ~Subfile() = default;

        virtual std::pair<std::vector<uint8_t>, std::vector<uint8_t>> serialize(uint32_t dataOffset,
                                        uint32_t extraDataOffset,
                                        const std::map<std::string, uint32_t, StringTableComparator>& stringTableOffsets) = 0;
        virtual std::set<std::string> getStrings() = 0;
        virtual uint32_t getDataLength() = 0;
        virtual uint32_t getAlignment() = 0;
        virtual SubfileType getType() = 0;
    };

    // Node is a struct that represents a node for the radix tree used in the indexes of the BFRES file.
    struct Node {
        uint32_t searchValue;
        uint16_t leftIdx;
        uint16_t rightIdx;
        std::optional<std::string> key;
        std::unique_ptr<Subfile> file = nullptr;
    };

    // BFRES is a class that represents a BFRES file. It currently does NOT support User Data nor
    // correctly writing Embedded Files, as it is not needed for Splatoon.
    class BFRES {
    public:
        BFRES(std::string fileName);
        ~BFRES() = default;

        void addSubfile(std::unique_ptr<Subfile> node, std::string key);
        std::vector<uint8_t> serialize();

    private:
        std::map<SubfileType, std::vector<Node>> subfiles;
        std::string fileName;
        int nSubfiles = 0;

        static std::pair<std::vector<uint8_t>, std::map<std::string, uint32_t, StringTableComparator>> serializeStringTable
            (const std::set<std::string, StringTableComparator>& strings, uint32_t offset);

        static void buildRadixTree(std::vector<Node>& nodes);
        static int getRadixTreeDirection(const std::string& key, uint32_t searchValue);

        constexpr static std::array<uint8_t, 4> BFRES_VERSION = {0x03, 0x05, 0x00, 0x03};
        constexpr static bool ENDIANNESS = false; // Big endian
    };

} // namespace boss::bfres

#endif //SPLATOON_SERVER_BFRES_HPP
