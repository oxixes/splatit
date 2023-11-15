#ifndef SPLATOON_SERVER_BYAML_HPP
#define SPLATOON_SERVER_BYAML_HPP

#include <string>
#include <map>
#include <set>
#include <vector>
#include <memory>

namespace boss::byaml {

    enum class NodeType {
        STRING = 0xA0,
        ARRAY = 0xC0,
        DICTIONARY = 0xC1,
        BOOL = 0xD0,
        INTEGER = 0xD1
    };

    class Node {
    public:
        Node() = default;
        virtual ~Node() = default;

        [[nodiscard]] NodeType getType() const { return type; }
        [[nodiscard]] virtual std::set<std::string> getKeys() const = 0;
        [[nodiscard]] virtual std::set<std::string> getStrings() const = 0;
        [[nodiscard]] virtual std::vector<uint8_t> serialize(const std::set<std::string>& keyTable,
                                                             const std::set<std::string>& stringTable,
                                                             uint32_t extraOffset, std::vector<uint8_t>& extra) const = 0;

    protected:
        NodeType type = NodeType::STRING;
    };

    class StringNode : public Node {
    public:
        explicit StringNode(std::string str);
        ~StringNode() override = default;

        [[nodiscard]] std::set<std::string> getKeys() const override;
        [[nodiscard]] std::set<std::string> getStrings() const override;
        [[nodiscard]] std::vector<uint8_t> serialize(const std::set<std::string>& keyTable,
                                                     const std::set<std::string>& stringTable,
                                                     uint32_t extraOffset, std::vector<uint8_t>& extra) const override;

    private:
        std::string str;
    };

    class ArrayNode : public Node {
    public:
        explicit ArrayNode(std::vector<std::shared_ptr<Node>> nodes);
        ~ArrayNode() override = default;

        [[nodiscard]] std::set<std::string> getKeys() const override;
        [[nodiscard]] std::set<std::string> getStrings() const override;
        [[nodiscard]] std::vector<uint8_t> serialize(const std::set<std::string>& keyTable,
                                                     const std::set<std::string>& stringTable,
                                                     uint32_t extraOffset, std::vector<uint8_t>& extra) const override;

    private:
        std::vector<std::shared_ptr<Node>> nodes;
    };

    class DictionaryNode : public Node {
    public:
        explicit DictionaryNode(std::map<std::string, std::shared_ptr<Node>> nodes);
        ~DictionaryNode() override = default;

        [[nodiscard]] std::set<std::string> getKeys() const override;
        [[nodiscard]] std::set<std::string> getStrings() const override;
        [[nodiscard]] std::vector<uint8_t> serialize(const std::set<std::string>& keyTable,
                                                     const std::set<std::string>& stringTable,
                                                     uint32_t extraOffset, std::vector<uint8_t>& extra) const override;

    private:
        std::map<std::string, std::shared_ptr<Node>> nodes;
    };

    class BoolNode : public Node {
    public:
        explicit BoolNode(bool value);
        ~BoolNode() override = default;

        [[nodiscard]] std::set<std::string> getKeys() const override;
        [[nodiscard]] std::set<std::string> getStrings() const override;
        [[nodiscard]] std::vector<uint8_t> serialize(const std::set<std::string>& keyTable,
                                                     const std::set<std::string>& stringTable,
                                                     uint32_t extraOffset, std::vector<uint8_t>& extra) const override;

    private:
        bool value;
    };

    class IntegerNode : public Node {
    public:
        explicit IntegerNode(int32_t value);
        ~IntegerNode() override = default;

        [[nodiscard]] std::set<std::string> getKeys() const override;
        [[nodiscard]] std::set<std::string> getStrings() const override;
        [[nodiscard]] std::vector<uint8_t> serialize(const std::set<std::string>& keyTable,
                                                     const std::set<std::string>& stringTable,
                                                     uint32_t extraOffset, std::vector<uint8_t>& extra) const override;

    private:
        int32_t value;
    };

    class Byaml {
    public:
        Byaml() = default;
        explicit Byaml(std::shared_ptr<Node> root) : root(std::move(root)) {}
        ~Byaml() = default;

        [[nodiscard]] std::vector<uint8_t> serialize() const;

    private:
        std::shared_ptr<Node> root;

        [[nodiscard]] static std::vector<uint8_t> serializeStringTable(const std::set<std::string>& stringTable);
    };

} // namespace boss

#endif //SPLATOON_SERVER_BYAML_HPP
