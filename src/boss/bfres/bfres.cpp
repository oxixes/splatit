#include "bfres.hpp"
#include "../../util/util.hpp"

#include <utility>
#include <cstring>
#include <algorithm>

namespace boss::bfres {

BFRES::BFRES(std::string fileName) {
    this->fileName = std::move(fileName);
    for (int i = 0; i < 12; i++) {
        subfiles[static_cast<SubfileType>(i)] = std::vector<Node>();
        subfiles[static_cast<SubfileType>(i)].emplace_back(); // Root node
    }
}

void BFRES::addSubfile(std::unique_ptr<Subfile> node, std::string key) {
    SubfileType type = node->getType();
    subfiles[type].emplace_back();
    Node& newNode = subfiles[type].back();
    newNode.file = std::move(node);
    newNode.key = std::move(key);
    nSubfiles++;
}

std::vector<uint8_t> BFRES::serialize() {
    if (nSubfiles == 0) throw std::runtime_error("No subfiles to serialize");

    std::set<std::string, StringTableComparator> strings;
    uint32_t dataLength = 0;
    uint32_t stringTableOffset = 0x6C; // header length

    uint32_t alignment = 8192;

    strings.insert(fileName);
    for (auto& [type, nodes] : subfiles) {
        for (auto& node : nodes) {
            if (node.file == nullptr) continue;
            for (const auto& str : node.file->getStrings()) {
                strings.insert(str);
            }
            strings.insert(node.key.value());

            dataLength += node.file->getDataLength();

            if (node.file->getDataLength() % 4 != 0) {
                // Align to 4 bytes
                dataLength += 4 - (node.file->getDataLength() % 4);
            }

            if (node.file->getAlignment() < alignment) {
                alignment = node.file->getAlignment();
            }
        }

        if (nodes.size() > 1) {
            stringTableOffset += 8 + 16 * nodes.size(); // Index groups
        }
    }

    stringTableOffset += dataLength;

    std::pair<std::vector<uint8_t>, std::map<std::string, uint32_t, StringTableComparator>> stringTableData =
        serializeStringTable(strings, stringTableOffset);

    uint32_t extraDataOffset = stringTableOffset + stringTableData.first.size();

    // HEADER
    std::vector<uint8_t> data = {0x46, 0x52, 0x45, 0x53}; // "FRES"
    data.insert(data.end(), BFRES_VERSION.begin(), BFRES_VERSION.end());
    if (ENDIANNESS) {
        data.push_back(0xFF);
        data.push_back(0xFE);
    } else {
        data.push_back(0xFE);
        data.push_back(0xFF);
    }
    data.push_back(0x00);
    data.push_back(0x10); // Header length

    // Insert 4 ceroes, where the file length will be written
    data.insert(data.end(), 4, 0);

    util::getu32Big(alignment);
    data.insert(data.end(), 4, 0);
    std::memcpy(data.data() + 0x10, &alignment, sizeof(uint32_t));

    uint32_t fileNameOffset = stringTableData.second[fileName] - 0x14;
    util::getu32Big(fileNameOffset);
    data.insert(data.end(), 4, 0);
    std::memcpy(data.data() + 0x14, &fileNameOffset, sizeof(uint32_t));

    uint32_t stringTableLength = stringTableData.first.size();
    util::getu32Big(stringTableLength);
    data.insert(data.end(), 4, 0);
    std::memcpy(data.data() + 0x18, &stringTableLength, sizeof(uint32_t));

    stringTableOffset -= 0x1C;
    util::getu32Big(stringTableOffset);
    data.insert(data.end(), 4, 0);
    std::memcpy(data.data() + 0x1C, &stringTableOffset, sizeof(uint32_t));

    // Index group offsets
    uint32_t indexGroupOffset = 0x6C;
    uint32_t currentOffset = 0x20;
    for (int i = 0; i < 12; i++) {
        if (subfiles[static_cast<SubfileType>(i)].size() > 1) {
            uint32_t indexGroupLength = 8 + 16 * subfiles[static_cast<SubfileType>(i)].size();
            uint32_t offset = indexGroupOffset - currentOffset;

            util::getu32Big(offset);
            data.insert(data.end(), 4, 0);
            std::memcpy(data.data() + currentOffset, &offset, sizeof(uint32_t));

            indexGroupOffset += indexGroupLength;
        } else {
            data.insert(data.end(), 4, 0);
        }

        currentOffset += 4;
    }

    for (int i = 0; i < 12; i++) {
        if (subfiles[static_cast<SubfileType>(i)].size() > 1) {
            uint16_t fileCount = subfiles[static_cast<SubfileType>(i)].size() - 1;
            util::getu16Big(fileCount);
            data.insert(data.end(), 2, 0);
            std::memcpy(data.data() + currentOffset, &fileCount, sizeof(uint16_t));
        } else {
            data.insert(data.end(), 2, 0);
        }

        currentOffset += 2;
    }

    data.insert(data.end(), 4, 0); // User pointer
    currentOffset += 4;

    // INDEX GROUPS
    uint32_t currentDataOffset = indexGroupOffset; // After all the index groups, the data starts
    for (int i = 0; i < 12; i++) {
        if (subfiles[static_cast<SubfileType>(i)].size() > 1) {
            std::vector<Node>& nodes = subfiles[static_cast<SubfileType>(i)];
            uint32_t indexGroupLength = 8 + 16 * nodes.size();
            std::vector<uint8_t> indexGroupData(indexGroupLength, 0);
            util::getu32Big(indexGroupLength);
            std::memcpy(indexGroupData.data(), &indexGroupLength, sizeof(uint32_t));

            uint32_t nodeCount = nodes.size() - 1;
            util::getu32Big(nodeCount);
            std::memcpy(indexGroupData.data() + 4, &nodeCount, sizeof(uint32_t));

            currentOffset += 8;

            buildRadixTree(nodes);
            uint32_t currentIndexGroupOffset = 8;
            for (const Node& node : nodes) {
                uint32_t searchValue = node.searchValue;
                uint16_t leftIdx = node.leftIdx;
                uint16_t rightIdx = node.rightIdx;
                uint32_t keyOffset = 0;
                uint32_t dataOffset = 0;
                if (node.file != nullptr) {
                    keyOffset = stringTableData.second[node.key.value()] - (currentOffset + 8);
                    dataOffset = currentDataOffset - (currentOffset + 12);
                }

                util::getu32Big(searchValue);
                util::getu16Big(leftIdx);
                util::getu16Big(rightIdx);
                util::getu32Big(keyOffset);
                util::getu32Big(dataOffset);

                std::memcpy(indexGroupData.data() + currentIndexGroupOffset, &searchValue, sizeof(uint32_t));
                std::memcpy(indexGroupData.data() + currentIndexGroupOffset + 4, &leftIdx, sizeof(uint16_t));
                std::memcpy(indexGroupData.data() + currentIndexGroupOffset + 6, &rightIdx, sizeof(uint16_t));
                std::memcpy(indexGroupData.data() + currentIndexGroupOffset + 8, &keyOffset, sizeof(uint32_t));
                std::memcpy(indexGroupData.data() + currentIndexGroupOffset + 12, &dataOffset, sizeof(uint32_t));

                currentOffset += 16;
                currentIndexGroupOffset += 16;
                if (node.file != nullptr) currentDataOffset += node.file->getDataLength();
                while (currentDataOffset % 4 != 0) currentDataOffset++; // Align to 4 bytes
            }

            data.insert(data.end(), indexGroupData.begin(), indexGroupData.end());
        }
    }

    currentDataOffset = indexGroupOffset;

    // DATA
    std::vector<uint8_t> extraData;
    for (auto& [type, nodes] : subfiles) {
        for (auto& node : nodes) {
            if (node.file == nullptr) continue;
            std::pair<std::vector<uint8_t>, std::vector<uint8_t>> serialized = node.file->
                    serialize(currentDataOffset, extraDataOffset, stringTableData.second);

            data.insert(data.end(), serialized.first.begin(), serialized.first.end());
            extraData.insert(extraData.end(), serialized.second.begin(), serialized.second.end());

            currentDataOffset += serialized.first.size(); // MUST match getDataLength()
            extraDataOffset += serialized.second.size();

            // Align data to 4 bytes
            while (data.size() % 4 != 0) {
                data.push_back(0);
                currentDataOffset++;
            }
        }
    }

    // STRING TABLE
    data.insert(data.end(), stringTableData.first.begin(), stringTableData.first.end());

    // EXTRA DATA
    data.insert(data.end(), extraData.begin(), extraData.end());

    // FILE LENGTH
    uint32_t fileLength = data.size();
    util::getu32Big(fileLength);
    std::memcpy(data.data() + 0xC, &fileLength, sizeof(uint32_t));

    return data;
}

std::pair<std::vector<uint8_t>, std::map<std::string, uint32_t, StringTableComparator>>
BFRES::serializeStringTable(const std::set<std::string, StringTableComparator>& strings, uint32_t offset) {
    std::vector<uint8_t> data;
    std::map<std::string, uint32_t, StringTableComparator> offsets;

    uint32_t currentOffset = offset;
    for (const auto& str : strings) {
        uint32_t length = str.length();

        uint32_t lengthToWrite = length;
        util::getu32Big(lengthToWrite);
        data.insert(data.end(), 4, 0);
        std::memcpy(data.data() + data.size() - 4, &lengthToWrite, sizeof(uint32_t));
        currentOffset += 4;

        offsets[str] = currentOffset;

        data.insert(data.end(), str.begin(), str.end());
        data.push_back(0); // Null terminator
        currentOffset += length + 1;

        // Align to 4 bytes
        while (data.size() % 4 != 0) {
            data.push_back(0);
            currentOffset++;
        }
    }

    return {data, offsets};
}

// Code based off of Syroot/NintenTools.Bfres at https://gitlab.com/Syroot/NintenTools/Bfres
// (https://gitlab.com/Syroot/NintenTools/Bfres/-/blob/master/src/Syroot.NintenTools.Bfres/Common/ResDict.cs?ref_type=heads#L493)
void BFRES::buildRadixTree(std::vector<Node>& nodes) {
    nodes[0] = Node(); // We start with an empty node as the root
    nodes[0].searchValue = 0xFFFFFFFF;
    nodes[0].key = "";

    for (int i = 1; i < nodes.size(); i++) {
        Node* current = &nodes[i];
        std::string key = current->key.value();

        // Iterate through the tree to get the string for bit comparison
        Node* parent = &nodes[0];
        Node* child = &nodes[parent->leftIdx];
        while (parent->searchValue > child->searchValue) {
            parent = child;
            child = &nodes[getRadixTreeDirection(key, parent->searchValue) == 0 ? child->leftIdx : child->rightIdx];
        }

        uint32_t searchValue = std::max(key.length(), child->key.value().length()) * 8;

        // Check for duplicate keys
        while (getRadixTreeDirection(child->key.value(), searchValue) == getRadixTreeDirection(key, searchValue)) {
            if (searchValue == 0) throw std::runtime_error("Duplicate key found in BFRES index: " + key);
            searchValue--;
        }

        current->searchValue = searchValue;

        // Form the tree
        parent = &nodes[0];
        child = &nodes[parent->leftIdx];
        uint16_t childIdx = parent->leftIdx;
        while (parent->searchValue > child->searchValue && child->searchValue > searchValue) {
            parent = child;
            childIdx = getRadixTreeDirection(key, parent->searchValue) == 0 ? child->leftIdx : child->rightIdx;
            child = &nodes[childIdx];
        }

        if (getRadixTreeDirection(key, searchValue) == 0) {
            current->leftIdx = i;
            current->rightIdx = childIdx;
        } else {
            current->leftIdx = childIdx;
            current->rightIdx = i;
        }

        if (getRadixTreeDirection(key, parent->searchValue) == 0) {
            parent->leftIdx = i;
        } else {
            parent->rightIdx = i;
        }
    }

    nodes[0].key.reset();
}

int BFRES::getRadixTreeDirection(const std::string& key, uint32_t searchValue) {
    if (searchValue >> 3 >= key.size()) return 0;

    char c = key[searchValue >> 3];
    return (c >> (searchValue & 0b00000111)) & 1;
}

} // namespace boss::bfres