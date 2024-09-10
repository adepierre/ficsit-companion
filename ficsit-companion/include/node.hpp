#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <imgui_node_editor.h>

#include "fractional_number.hpp"
#include "json.hpp"

struct Item;
struct Pin;
struct Recipe;

struct Node
{
    enum class Kind
    {
        Craft,
        Splitter,
        Merger
    };

    Node(const ax::NodeEditor::NodeId id);
    virtual ~Node();

    virtual Kind GetKind() const = 0;
    virtual bool IsCraft() const;
    virtual bool IsOrganizer() const;
    virtual bool IsMerger() const;
    virtual bool IsSplitter() const;
    virtual Json::Value Serialize() const;
    virtual bool Deserialize(const Json::Value& v);

    const ax::NodeEditor::NodeId id;

    std::vector<std::unique_ptr<Pin>> ins;
    std::vector<std::unique_ptr<Pin>> outs;
    ImVec2 pos;
};

struct CraftNode : public Node
{
    CraftNode(const ax::NodeEditor::NodeId id, const Recipe* recipe,
        const std::function<unsigned long long int()>& id_generator);
    virtual ~CraftNode();
    virtual Kind GetKind() const override;
    virtual bool IsCraft() const override;
    virtual Json::Value Serialize() const override;
    virtual bool Deserialize(const Json::Value& v) override;

    const Recipe* recipe;
    FractionalNumber current_rate;
    /// @brief Technically it could be just an int, but FractionalNumber already has all string operations
    FractionalNumber num_somersloop;
};

struct OrganizerNode : public Node
{
    OrganizerNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Item* item = nullptr);
    virtual ~OrganizerNode();
    virtual bool IsOrganizer() const override;
    virtual Json::Value Serialize() const override;
    virtual bool Deserialize(const Json::Value& v) override;

    void ChangeItem(const Item* item);
    void RemoveItemIfNotForced();
    bool IsBalanced() const;

    const Item* item;
};

struct SplitterNode : public OrganizerNode
{
    SplitterNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Item* item = nullptr);
    virtual ~SplitterNode();
    virtual bool IsSplitter() const override;

    virtual Kind GetKind() const override;
};

struct MergerNode : public OrganizerNode
{
    MergerNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Item* item = nullptr);
    virtual ~MergerNode();
    virtual bool IsMerger() const override;

    virtual Kind GetKind() const override;
};
