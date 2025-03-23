#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <imgui_node_editor.h>

#include "fractional_number.hpp"
#include "json.hpp"
#include "utils.hpp"

struct Item;
struct Link;
struct Pin;
struct Recipe;

struct Node
{
    /// @brief Type of node, ALWAYS ADD NEW TYPES AT THE END (would break old save files otherwise)
    enum class Kind
    {
        Craft,
        CustomSplitter,
        Merger,
        Group,
        GameSplitter,
        Sink,
    };

    Node(const ax::NodeEditor::NodeId id);
    Node(const ax::NodeEditor::NodeId id, const Json::Value& serialized);
    virtual ~Node();

    virtual Kind GetKind() const = 0;
    virtual bool IsPowered() const;
    virtual bool IsCraft() const;
    virtual bool IsGroup() const;
    /// @brief Merger or Splitter
    virtual bool IsOrganizer() const;
    virtual bool IsMerger() const;
    virtual bool IsCustomSplitter() const;
    virtual bool IsGameSplitter() const;
    virtual bool IsSink() const;
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
    /// @brief Custom boolean that can be used to track progress on factory building
    bool built;
};

struct GroupNode : public PoweredNode
{
    GroupNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator,
        std::vector<std::unique_ptr<Node>>&& nodes_, std::vector<std::unique_ptr<Link>>&& links_);
    GroupNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator,
        const Json::Value& serialized);
    virtual Kind GetKind() const override;
    virtual bool IsGroup() const override;
    virtual Json::Value Serialize() const override;
    virtual void UpdateRate(const FractionalNumber& new_rate) override;
    virtual bool HasVariablePower() const override;
    virtual void ComputePowerUsage() override;
    void PropagateRateToSubnodes();
    void SetBuiltState(const bool b);

private:
    void CreateInsOuts(const std::function<unsigned long long int()>& id_generator);
    void UpdateDetails();

public:
    std::vector<std::unique_ptr<Node>> nodes;
    /// @brief The rate of the node when this group was created.
    /// Required cause in case the group rate is set to 0 the info is lost otherwise
    std::vector<FractionalNumber> nodes_base_rate;
    std::vector<std::unique_ptr<Link>> links;

    std::string name;
    /// @brief Cached value to avoid looping through all the nodes everytime
    bool variable_power;
    std::map<std::string, FractionalNumber> total_machines;
    std::map<std::string, FractionalNumber> built_machines;
    std::map<std::string, std::map<const Recipe*, FractionalNumber>> detailed_machines;
    std::map<const Recipe*, FractionalNumber> detailed_power_same_clock;
    std::map<const Recipe*, FractionalNumber> detailed_power_last_underclock;
    std::map<const Item*, FractionalNumber, ItemPtrCompare> inputs;
    std::map<const Item*, FractionalNumber, ItemPtrCompare> outputs;
    std::map<const Item*, FractionalNumber> detailed_sinked_points;
    bool loading_error;
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
    virtual bool IsBalanced() const;

    const Item* item;
};

struct CustomSplitterNode : public OrganizerNode
{
    CustomSplitterNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Item* item = nullptr);
    CustomSplitterNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Json::Value& serialized);
    virtual ~CustomSplitterNode();
    virtual bool IsCustomSplitter() const override;

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

struct GameSplitterNode : public OrganizerNode
{
    GameSplitterNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Item* item = nullptr);
    GameSplitterNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Json::Value& serialized);
    virtual ~GameSplitterNode();
    virtual bool IsGameSplitter() const override;

    virtual Kind GetKind() const override;
    virtual bool IsBalanced() const override;
};

struct SinkNode : public Node
{
    SinkNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Item* item = nullptr);
    SinkNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Json::Value& serialized);
    virtual ~SinkNode();
    virtual bool IsSink() const override;

    virtual Kind GetKind() const override;
    virtual Json::Value Serialize() const override;
};
