#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <imgui_node_editor.h>

#include "fractional_number.hpp"

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

    const ax::NodeEditor::NodeId id;

    std::vector<std::unique_ptr<Pin>> ins;
    std::vector<std::unique_ptr<Pin>> outs;
};

struct CraftNode : public Node
{
    CraftNode(const ax::NodeEditor::NodeId id, const Recipe* recipe,
        const std::function<unsigned long long int()>& id_generator);
    virtual ~CraftNode();

    virtual Kind GetKind() const override;

    const Recipe* recipe;
    FractionalNumber current_rate;
};

struct OrganizerNode : public Node
{
    OrganizerNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator);
    virtual ~OrganizerNode();

    void ChangeItem(const Item* item);
    void RemoveItemIfNotForced();
    bool IsBalanced() const;

    const Item* item;
};

struct SplitterNode : public OrganizerNode
{
    SplitterNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator);
    virtual ~SplitterNode();

    virtual Kind GetKind() const override;
};

struct MergerNode : public OrganizerNode
{
    MergerNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator);
    virtual ~MergerNode();

    virtual Kind GetKind() const override;
};
