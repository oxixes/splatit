#include <cstring>
#include <iostream>

#include "byaml.hpp"
#include "../../util/util.hpp"

namespace boss::byaml {

StringNode::StringNode(std::string str) : str(std::move(str)) { type = NodeType::STRING; }
std::set<std::string> StringNode::getKeys() const { return {}; }
std::set<std::string> StringNode::getStrings() const { return {str}; }
std::vector<uint8_t> StringNode::serialize(const std::set<std::string>& keyTable, const std::set<std::string>& stringTable,
                                           uint32_t extraOffset, std::vector<uint8_t>& extra) const {
    uint32_t strIdx = std::distance(stringTable.begin(), stringTable.find(str));
    util::getu32Big(strIdx);

    std::vector<uint8_t> data(4);
    std::memcpy(data.data(), &strIdx, sizeof(uint32_t));

    return data;
}

ArrayNode::ArrayNode(std::vector<std::shared_ptr<Node>> nodes) : nodes(std::move(nodes)) { type = NodeType::ARRAY; }
std::set<std::string> ArrayNode::getKeys() const {
    std::set<std::string> keys;
    for (auto &node: nodes) {
        auto nodeKeys = node->getKeys();
        keys.insert(nodeKeys.begin(), nodeKeys.end());
    }

    return keys;
}

std::set<std::string> ArrayNode::getStrings() const {
    std::set<std::string> strings;
    for (auto& node: nodes) {
        auto nodeStrings = node->getStrings();
        strings.insert(nodeStrings.begin(), nodeStrings.end());
    }

    return strings;
}

std::vector<uint8_t> ArrayNode::serialize(const std::set<std::string>& keyTable, const std::set<std::string>& stringTable,
                                          uint32_t extraOffset, std::vector<uint8_t>& extra) const {
    size_t alignedSize = (nodes.size() + 3) & ~0x3;

    std::vector<uint8_t> data(4 + alignedSize + nodes.size() * 4, 0);
    data[0] = 0xC0; // Array node

    uint32_t arraySize = nodes.size();
    util::getu32Big(arraySize);
    std::memcpy(data.data() + 1, ((uint8_t*) &arraySize) + 1, sizeof(uint32_t) - 1); // We want to write 3 bytes, not 4

    uint32_t nodesExtraOffset = extraOffset + data.size();
    std::vector<uint8_t> newExtra;

    uint32_t i = 0;
    for (auto& node: nodes) {
        data[4 + i] = static_cast<uint8_t>(node->getType());

        std::vector<uint8_t> nodeData = node->serialize(keyTable, stringTable, nodesExtraOffset, newExtra);
        nodesExtraOffset = extraOffset + data.size() + newExtra.size();
        std::memcpy(data.data() + 4 + alignedSize + i * 4, nodeData.data(), sizeof(uint32_t));

        i++;
    }

    data.insert(data.end(), newExtra.begin(), newExtra.end());
    extra.insert(extra.end(), data.begin(), data.end());

    util::getu32Big(extraOffset);
    std::vector<uint8_t> result(4);
    std::memcpy(result.data(), &extraOffset, sizeof(uint32_t));

    return result;
}

DictionaryNode::DictionaryNode(std::map<std::string, std::shared_ptr<Node>> nodes) : nodes(std::move(nodes)) { type = NodeType::DICTIONARY; }
std::set<std::string> DictionaryNode::getKeys() const {
    std::set<std::string> keys;
    for (auto &node: nodes) {
        keys.insert(node.first);
        auto nodeKeys = node.second->getKeys();
        keys.insert(nodeKeys.begin(), nodeKeys.end());
    }

    return keys;
}

std::set<std::string> DictionaryNode::getStrings() const {
    std::set<std::string> strings;
    for (auto& node: nodes) {
        auto nodeStrings = node.second->getStrings();
        strings.insert(nodeStrings.begin(), nodeStrings.end());
    }

    return strings;
}

std::vector<uint8_t> DictionaryNode::serialize(const std::set<std::string>& keyTable, const std::set<std::string>& stringTable,
                                               uint32_t extraOffset, std::vector<uint8_t>& extra) const {
    std::vector<uint8_t> data(4 + nodes.size() * 8);
    data[0] = 0xC1; // Dictionary node

    uint32_t dictSize = nodes.size();
    util::getu32Big(dictSize);
    std::memcpy(data.data() + 1, ((uint8_t*) &dictSize) + 1, sizeof(uint32_t) - 1); // We want to write 3 bytes, not 4

    uint32_t nodesExtraOffset = extraOffset + data.size();
    std::vector<uint8_t> newExtra;

    uint32_t i = 0;
    for (auto& node : nodes) {
        uint32_t keyIdx = std::distance(keyTable.begin(), keyTable.find(node.first));
        util::getu32Big(keyIdx);
        std::memcpy(data.data() + 4 + i * 8, ((uint8_t*) &keyIdx) + 1, sizeof(uint32_t) - 1); // We want to write 3 bytes, not 4

        data[4 + i * 8 + 3] = static_cast<uint8_t>(node.second->getType());

        std::vector<uint8_t> nodeData = node.second->serialize(keyTable, stringTable, nodesExtraOffset, newExtra);
        nodesExtraOffset = extraOffset + data.size() + newExtra.size();
        std::memcpy(data.data() + 4 + i * 8 + 4, nodeData.data(), sizeof(uint32_t));

        i++;
    }

    data.insert(data.end(), newExtra.begin(), newExtra.end());
    extra.insert(extra.end(), data.begin(), data.end());

    util::getu32Big(extraOffset);
    std::vector<uint8_t> result(4);
    std::memcpy(result.data(), &extraOffset, sizeof(uint32_t));

    return result;
}

BoolNode::BoolNode(bool value) : value(value) { type = NodeType::BOOL; }
std::set<std::string> BoolNode::getKeys() const { return {}; }
std::set<std::string> BoolNode::getStrings() const { return {}; }
std::vector<uint8_t> BoolNode::serialize(const std::set<std::string>& keyTable, const std::set<std::string>& stringTable,
                                         uint32_t extraOffset, std::vector<uint8_t>& extra) const {
    if (value) {
        return {0x00, 0x00, 0x00, 0x01};
    } else {
        return {0x00, 0x00, 0x00, 0x00};
    }
}

IntegerNode::IntegerNode(int32_t value) : value(value) { type = NodeType::INTEGER; }
std::set<std::string> IntegerNode::getKeys() const { return {}; }
std::set<std::string> IntegerNode::getStrings() const { return {}; }
std::vector<uint8_t> IntegerNode::serialize(const std::set<std::string>& keyTable, const std::set<std::string>& stringTable,
                                            uint32_t extraOffset, std::vector<uint8_t>& extra) const {
    uint32_t valueBig;
    std::memcpy(&valueBig, &value, sizeof(uint32_t)); // We want to keep the same bytes. We don't care about its actual value here.
    util::getu32Big(valueBig);

    std::vector<uint8_t> data(4);
    std::memcpy(data.data(), &valueBig, sizeof(uint32_t));

    return data;
}

std::vector<uint8_t> Byaml::serialize() const {
    std::set<std::string> keyTable = root->getKeys();
    std::set<std::string> stringTable = root->getStrings();

    std::vector<uint8_t> data = {0x42, 0x59, 0x00, 0x01}; // BY (Big Endian) (Version 1)
    data.resize(0x10); // The header is 0x10 bytes long, so we need to reserve that much space

    // Write the dictionary key table
    uint32_t keyTableOffset = data.size();
    util::getu32Big(keyTableOffset);
    std::memcpy(data.data() + 0x4, &keyTableOffset, sizeof(uint32_t));

    auto keyTableData = serializeStringTable(keyTable);
    data.insert(data.end(), keyTableData.begin(), keyTableData.end());

    data.resize((data.size() + 3) & ~0x3, 0); // Align the data to 4 bytes

    // Write the string table
    uint32_t stringTableOffset = data.size();
    util::getu32Big(stringTableOffset);
    std::memcpy(data.data() + 0x8, &stringTableOffset, sizeof(uint32_t));

    auto stringTableData = serializeStringTable(stringTable);
    data.insert(data.end(), stringTableData.begin(), stringTableData.end());

    data.resize((data.size() + 3) & ~0x3, 0); // Align the data to 4 bytes

    // Write the root node
    if (root->getType() != NodeType::DICTIONARY && root->getType() != NodeType::ARRAY) {
        throw std::runtime_error("Root node must be a dictionary or array");
    }

    uint32_t currentOffset = data.size();
    std::vector<uint8_t> extra;
    auto rootNodeData = root->serialize(keyTable, stringTable, currentOffset, extra);
    std::memcpy(data.data() + 0xC, rootNodeData.data(), sizeof(uint32_t)); // Write the offset to the root node
    data.insert(data.end(), extra.begin(), extra.end());

    data.resize((data.size() + 3) & ~0x3, 0); // Align the data to 4 bytes

    return data;
}

std::vector<uint8_t> Byaml::serializeStringTable(const std::set<std::string>& stringTable) {
    std::vector<uint8_t> data(4 + (stringTable.size() + 1) * 4);
    data[0] = 0xC2; // String table node

    uint32_t stringTableSize = stringTable.size();
    util::getu32Big(stringTableSize);
    std::memcpy(data.data() + 1, ((uint8_t*) &stringTableSize) + 1, sizeof(uint32_t) - 1); // We want to write 3 bytes, not 4

    uint32_t i = 0;
    for (auto& str: stringTable) {
        uint32_t strOffset = data.size();
        uint32_t strOffsetBig = strOffset;
        util::getu32Big(strOffsetBig);

        std::memcpy(data.data() + 4 + i * 4, &strOffsetBig, sizeof(uint32_t));
        data.resize(data.size() + str.size() + 1);
        std::memcpy(data.data() + strOffset, str.c_str(), str.size());
        // Set the null terminator
        data[strOffset + str.size()] = 0x00;

        i++;
    }

    // Write the offset to the end of the string table
    uint32_t endOffset = data.size();
    util::getu32Big(endOffset);
    std::memcpy(data.data() + 4 + stringTable.size() * 4, &endOffset, sizeof(uint32_t));

    return data;
}

} // namespace boss