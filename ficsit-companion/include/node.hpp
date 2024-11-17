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
    /// @brief Type of node, ALWAYS ADD NEW TYPES AT THE END (would break old save files otherwise)
    enum class Kind
    {
        Craft,
        Splitter,
        Merger,
    };

    Node(const ax::NodeEditor::NodeId id);
    Node(const ax::NodeEditor::NodeId id, const Json::Value& serialized);
    virtual ~Node();

    virtual Kind GetKind() const = 0;
    virtual bool IsPowered() const;
    virtual bool IsCraft() const;
    /// @brief Merger or Splitter
    virtual bool IsOrganizer() const;
    virtual bool IsMerger() const;
    virtual bool IsSplitter() const;
    virtual Json::Value Serialize() const;

    static std::unique_ptr<Node> Deserialize(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Json::Value& serialized);

    const ax::NodeEditor::NodeId id;

    std::vector<std::unique_ptr<Pin>> ins;
    std::vector<std::unique_ptr<Pin>> outs;
    ImVec2 pos;
};

struct PoweredNode : public Node
{
    PoweredNode(const ax::NodeEditor::NodeId id);
    PoweredNode(const ax::NodeEditor::NodeId id, const Json::Value& serialized);
    virtual ~PoweredNode();

    virtual bool IsPowered() const override;
    virtual Json::Value Serialize() const override;
    virtual void UpdateRate(const FractionalNumber& new_rate) = 0;
    virtual void ComputePowerUsage() = 0;
    virtual bool HasVariablePower() const = 0;

    FractionalNumber current_rate;
    /// @brief Power requirement if all machines are at the same clock.
    /// It could be a double, but FractionalNumber already has all string operations
    FractionalNumber same_clock_power;
    /// @brief Power requirements if all machines are at 100% except the last one.
    /// It could be a double, but FractionalNumber already has all string operations
    FractionalNumber last_underclock_power;
};

struct CraftNode : public PoweredNode
{
    CraftNode(const ax::NodeEditor::NodeId id, const Recipe* recipe,
        const std::function<unsigned long long int()>& id_generator);
    CraftNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Json::Value& serialized);
    virtual ~CraftNode();
    virtual Kind GetKind() const override;
    virtual bool IsCraft() const override;
    virtual Json::Value Serialize() const override;
    virtual void UpdateRate(const FractionalNumber& new_rate) override;
    virtual bool HasVariablePower() const override;
    virtual void ComputePowerUsage() override;
    void ChangeRecipe(const Recipe* recipe, const std::function<unsigned long long int()>& id_generator);

    const Recipe* recipe;
    /// @brief Technically it could be just an int, but FractionalNumber already has all string operations
    FractionalNumber num_somersloop;
};

struct OrganizerNode : public Node
{
    OrganizerNode(const ax::NodeEditor::NodeId id, const Item* item = nullptr);
    OrganizerNode(const ax::NodeEditor::NodeId id, const Json::Value& serialized);
    virtual ~OrganizerNode();
    virtual bool IsOrganizer() const override;
    virtual Json::Value Serialize() const override;

    void ChangeItem(const Item* item);
    void RemoveItemIfNotForced();
    bool IsBalanced() const;

    const Item* item;
};

struct SplitterNode : public OrganizerNode
{
    SplitterNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Item* item = nullptr);
    SplitterNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Json::Value& serialized);
    virtual ~SplitterNode();
    virtual bool IsSplitter() const override;

    virtual Kind GetKind() const override;
};

struct MergerNode : public OrganizerNode
{
    MergerNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Item* item = nullptr);
    MergerNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Json::Value& serialized);
    virtual ~MergerNode();
    virtual bool IsMerger() const override;

    virtual Kind GetKind() const override;
};
