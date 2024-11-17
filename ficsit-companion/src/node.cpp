#include "building.hpp"
#include "game_data.hpp"
#include "node.hpp"
#include "pin.hpp"
#include "recipe.hpp"

#include <cmath>

#include <imgui_node_editor.h>

Node::Node(const ax::NodeEditor::NodeId id) : id(id)
{

}

Node::Node(const ax::NodeEditor::NodeId id, const Json::Value& serialized) : id(id)
{
    pos.x = serialized["pos"]["x"].get<float>();
    pos.y = serialized["pos"]["y"].get<float>();
}

Node::~Node()
{

}

bool Node::IsPowered() const
{
    return false;
}

bool Node::IsCraft() const
{
    return false;
}

bool Node::IsOrganizer() const
{
    return false;
}

bool Node::IsMerger() const
{
    return false;
}

bool Node::IsSplitter() const
{
    return false;
}

Json::Value Node::Serialize() const
{
    Json::Value node;
    node["kind"] = static_cast<int>(GetKind());
    node["pos"] = {
        { "x", pos.x },
        { "y", pos.y }
    };

    return node;
}

std::unique_ptr<Node> Node::Deserialize(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Json::Value& serialized)
{
    const Kind kind = static_cast<Node::Kind>(serialized["kind"].get<int>());
    switch (kind)
    {
    case Kind::Craft:
        return std::make_unique<CraftNode>(id, id_generator, serialized);
    case Kind::Merger:
        return std::make_unique<MergerNode>(id, id_generator, serialized);
    case Kind::Splitter:
        return std::make_unique<SplitterNode>(id, id_generator, serialized);
    default: // To make compilers happy, but should never happen
        throw std::domain_error("Unimplemented node type in Deserialize");
        return nullptr;
    }

    return nullptr;
}

PoweredNode::PoweredNode(const ax::NodeEditor::NodeId id) : Node(id), current_rate(1, 1), same_clock_power(0, 1), last_underclock_power(0, 1)
{

}

PoweredNode::PoweredNode(const ax::NodeEditor::NodeId id, const Json::Value& serialized) : Node(id, serialized), same_clock_power(0, 1), last_underclock_power(0, 1)
{
    const Kind kind = static_cast<Kind>(serialized["kind"].get<int>());
    if (kind != Kind::Craft)
    {
        throw std::runtime_error("Trying to deserialize an unvalid node as a powered node");
    }
    current_rate = FractionalNumber(serialized["rate"]["num"].get<long long int>(), serialized["rate"]["den"].get<long long int>());
}

PoweredNode::~PoweredNode()
{

}

bool PoweredNode::IsPowered() const
{
    return true;
}

Json::Value PoweredNode::Serialize() const
{
    Json::Value node = Node::Serialize();
    node["rate"] = {
        { "num", current_rate.GetNumerator()},
        { "den", current_rate.GetDenominator()}
    };
    return node;
}

CraftNode::CraftNode(const ax::NodeEditor::NodeId id, const Recipe* recipe, const std::function<unsigned long long int()>& id_generator) :
    PoweredNode(id), num_somersloop(0)
{
    ChangeRecipe(recipe, id_generator);
}

CraftNode::CraftNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Json::Value& serialized) :
    PoweredNode(id, serialized)
{
    if (static_cast<Kind>(serialized["kind"].get<int>()) != Kind::Craft)
    {
        throw std::runtime_error("Trying to deserialize an unvalid node as a craft node");
    }
    recipe = nullptr;
    const std::string& recipe_name = serialized["recipe"].get_string();
    const auto it = std::find_if(Data::Recipes().begin(), Data::Recipes().end(), [&recipe_name](const std::unique_ptr<Recipe>& recipe) { return recipe->name == recipe_name; });

    if (it != Data::Recipes().end())
    {
        ChangeRecipe(it->get(), id_generator);
    }
    else
    {
        throw std::runtime_error("Unknown recip when loading craft node");
    }

    num_somersloop = FractionalNumber(serialized["num_somersloop"].get<long long int>());
    ComputePowerUsage();
    for (auto& p : ins)
    {
        p->current_rate = p->base_rate * current_rate;
    }
    for (auto& p : outs)
    {
        p->current_rate = p->base_rate * current_rate * (1 + num_somersloop * recipe->building->somersloop_mult);
    }
}

CraftNode::~CraftNode()
{

}

bool CraftNode::IsCraft() const
{
    return true;
}

Json::Value CraftNode::Serialize() const
{
    Json::Value node = PoweredNode::Serialize();

    node["recipe"] = recipe->name;
    node["num_somersloop"] = num_somersloop.GetNumerator();

    return node;
}

void CraftNode::UpdateRate(const FractionalNumber& new_rate)
{
    current_rate = new_rate;
    for (auto& p : ins)
    {
        p->current_rate = p->base_rate * current_rate;
    }
    for (auto& p : outs)
    {
        p->current_rate = p->base_rate * current_rate * (1 + (num_somersloop * recipe->building->somersloop_mult));
    }
    ComputePowerUsage();
}

bool CraftNode::HasVariablePower() const
{
    return recipe->building->variable_power;
}

void CraftNode::ComputePowerUsage()
{
    const Building* building = recipe->building;
    // All machines are underclocked at current_rate/num_machines
    const int num_machines = static_cast<int>(std::ceil(current_rate.GetValue()));
    const double power = recipe->power;
    double same_clock_power_double =
        num_machines *
        power *
        std::pow(1.0 + num_somersloop.GetValue() * building->somersloop_mult.GetValue(), building->somersloop_power_exponent) *
        std::pow(current_rate.GetValue() / static_cast<double>(std::max(1, num_machines)), building->power_exponent);
    // num_full_machines at 100% rate + one extra underclocked machine
    const int num_full_machines = static_cast<int>(std::floor(current_rate.GetValue()));
    double last_underclock_power_double =
        num_full_machines *
        power *
        std::pow(1.0 + num_somersloop.GetValue() * building->somersloop_mult.GetValue(), building->somersloop_power_exponent);
    last_underclock_power_double +=
        power *
        std::pow(1.0 + num_somersloop.GetValue() * building->somersloop_mult.GetValue(), building->somersloop_power_exponent) *
        std::pow(current_rate.GetValue() - num_full_machines, building->power_exponent);
    // Round values at 0.001 precision for the power, as we don't have exact fractional values with the exponents anyway
    same_clock_power = FractionalNumber(static_cast<long long int>(std::round(same_clock_power_double * 1000.0)), 1000);
    last_underclock_power = FractionalNumber(static_cast<long long int>(std::round(last_underclock_power_double * 1000.0)), 1000);

}

void CraftNode::ChangeRecipe(const Recipe* recipe, const std::function<unsigned long long int()>& id_generator)
{
    this->recipe = recipe;
    if (recipe == nullptr)
    {
        ins.clear();
        outs.clear();
        return;
    }

    ComputePowerUsage();
    for (const auto& input : recipe->ins)
    {
        ins.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Input, this, input.item, input.quantity));
    }
    for (const auto& output : recipe->outs)
    {
        outs.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Output, this, output.item, output.quantity));
    }
}

Node::Kind CraftNode::GetKind() const
{
    return Node::Kind::Craft;
}

OrganizerNode::OrganizerNode(const ax::NodeEditor::NodeId id, const Item* item) : Node(id)
{
    ChangeItem(item);
}

OrganizerNode::OrganizerNode(const ax::NodeEditor::NodeId id, const Json::Value& serialized) : Node(id, serialized), item(nullptr)
{
    const Kind kind = static_cast<Kind>(serialized["kind"].get<int>());
    if (kind != Kind::Merger && kind != Kind::Splitter)
    {
        throw std::runtime_error("Trying to deserialize an unvalid node as an organizer node");
    }
    auto item_it = Data::Items().find(serialized["item"].get_string());
    if (item_it != Data::Items().end())
    {
        ChangeItem(item_it->second.get());
    }
    else if (serialized["item"].get_string() != "")
    {
        throw std::runtime_error("Unknown item when loading organizer node");
    }
}

OrganizerNode::~OrganizerNode()
{

}

bool OrganizerNode::IsOrganizer() const
{
    return true;
}

Json::Value OrganizerNode::Serialize() const
{
    Json::Value serialized = Node::Serialize();

    serialized["item"] = item == nullptr ? "" : item->name;

    Json::Array ins_array;
    ins_array.reserve(ins.size());
    for (auto& i : ins)
    {
        ins_array.push_back({
            { "num", i->current_rate.GetNumerator() },
            { "den", i->current_rate.GetDenominator() },
        });
    }
    serialized["ins"] = ins_array;

    Json::Array outs_array;
    outs_array.reserve(outs.size());
    for (auto& o : outs)
    {
        outs_array.push_back({
            { "num", o->current_rate.GetNumerator() },
            { "den", o->current_rate.GetDenominator() },
        });
    }
    serialized["outs"] = outs_array;

    return serialized;
}

void OrganizerNode::ChangeItem(const Item* item)
{
    this->item = item;
    for (auto& p : ins)
    {
        p->item = item;
    }
    for (auto& p : outs)
    {
        p->item = item;
    }
}

void OrganizerNode::RemoveItemIfNotForced()
{
    if (item == nullptr)
    {
        return;
    }

    for (auto& p : ins)
    {
        if (p->link != nullptr)
        {
            return;
        }
    }

    for (auto& p : outs)
    {
        if (p->link != nullptr)
        {
            return;
        }
    }

    ChangeItem(nullptr);
}

bool OrganizerNode::IsBalanced() const
{
    FractionalNumber input_sum(0, 1);
    for (const auto& p : ins)
    {
        input_sum += p->current_rate;
    }
    FractionalNumber output_sum(0, 1);
    for (const auto& p : outs)
    {
        output_sum += p->current_rate;
    }

    return input_sum == output_sum;
}

SplitterNode::SplitterNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Item* item) : OrganizerNode(id, item)
{
    ins.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Input, this, nullptr));
    outs.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Output, this, nullptr));
    outs.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Output, this, nullptr));
    // We need to call ChangeItem again to update newly created pins
    ChangeItem(item);
}

SplitterNode::SplitterNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Json::Value& serialized) : OrganizerNode(id, serialized)
{
    if (static_cast<Kind>(serialized["kind"].get<int>()) != Kind::Splitter)
    {
        throw std::runtime_error("Trying to deserialize an unvalid node as a splitter node");
    }
    ins.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Input, this, nullptr));
    outs.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Output, this, nullptr));
    outs.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Output, this, nullptr));
    // We need to call ChangeItem again to update newly created pins
    ChangeItem(item);

    for (int i = 0; i < serialized["ins"].size(); ++i)
    {
        if (i >= ins.size())
        {
            break;
        }
        ins[i]->current_rate = FractionalNumber(serialized["ins"][i]["num"].get<long long int>(), serialized["ins"][i]["den"].get<long long int>());
    }
    for (int i = 0; i < serialized["outs"].size(); ++i)
    {
        if (i >= outs.size())
        {
            break;
        }
        outs[i]->current_rate = FractionalNumber(serialized["outs"][i]["num"].get<long long int>(), serialized["outs"][i]["den"].get<long long int>());
    }
}

SplitterNode::~SplitterNode()
{
}

bool SplitterNode::IsSplitter() const
{
    return true;
}

Node::Kind SplitterNode::GetKind() const
{
    return Node::Kind::Splitter;
}

MergerNode::MergerNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Item* item) : OrganizerNode(id, item)
{
    ins.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Input, this, nullptr));
    ins.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Input, this, nullptr));
    outs.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Output, this, nullptr));
    // We need to call ChangeItem again to update newly created pins
    ChangeItem(item);
}

MergerNode::MergerNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Json::Value& serialized) : OrganizerNode(id, serialized)
{
    if (static_cast<Kind>(serialized["kind"].get<int>()) != Kind::Merger)
    {
        throw std::runtime_error("Trying to deserialize an unvalid node as a craft node");
    }
    ins.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Input, this, nullptr));
    ins.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Input, this, nullptr));
    outs.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Output, this, nullptr));
    // We need to call ChangeItem again to update newly created pins
    ChangeItem(item);

    for (int i = 0; i < serialized["ins"].size(); ++i)
    {
        if (i >= ins.size())
        {
            break;
        }
        ins[i]->current_rate = FractionalNumber(serialized["ins"][i]["num"].get<long long int>(), serialized["ins"][i]["den"].get<long long int>());
    }
    for (int i = 0; i < serialized["outs"].size(); ++i)
    {
        if (i >= outs.size())
        {
            break;
        }
        outs[i]->current_rate = FractionalNumber(serialized["outs"][i]["num"].get<long long int>(), serialized["outs"][i]["den"].get<long long int>());
    }
}

MergerNode::~MergerNode()
{

}

bool MergerNode::IsMerger() const
{
    return true;
}

Node::Kind MergerNode::GetKind() const
{
    return Node::Kind::Merger;
}
