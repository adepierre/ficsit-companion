#include "building.hpp"
#include "game_data.hpp"
#include "link.hpp"
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

bool Node::IsGroup() const
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

bool Node::IsCustomSplitter() const
{
    return false;
}

bool Node::IsGameSplitter() const
{
    return false;
}

bool Node::IsSink() const
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
    case Kind::CustomSplitter:
        return std::make_unique<CustomSplitterNode>(id, id_generator, serialized);
    case Kind::Group:
        return std::make_unique<GroupNode>(id, id_generator, serialized);
    case Kind::GameSplitter:
        return std::make_unique<GameSplitterNode>(id, id_generator, serialized);
    case Kind::Sink:
        return std::make_unique<SinkNode>(id, id_generator, serialized);
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
    if (kind != Kind::Craft && kind != Kind::Group)
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
    PoweredNode(id), num_somersloop(0), built(false)
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
        throw std::runtime_error("Unknown recipe when loading craft node");
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
    built = serialized["built"].get<bool>();
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
    node["built"] = built;

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

GroupNode::GroupNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator,
    std::vector<std::unique_ptr<Node>>&& nodes_, std::vector<std::unique_ptr<Link>>&& links_) :
    PoweredNode(id), nodes(std::move(nodes_)), links(std::move(links_)), name(""), variable_power(false), loading_error(false)
{
    CreateInsOuts(id_generator);
    ComputePowerUsage();
    UpdateDetails();
}

GroupNode::GroupNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Json::Value& serialized) : PoweredNode(id, serialized)
{
    if (static_cast<Kind>(serialized["kind"].get<int>()) != Kind::Group)
    {
        throw std::runtime_error("Trying to deserialize an unvalid node as a group node");
    }
    name = serialized["name"].get_string();

    unsigned long long int current_id = 0;
    auto local_id_generator = [&]() { return current_id++; };

    std::vector<int> node_indices;
    node_indices.reserve(serialized["nodes"].size());
    size_t num_nodes = 0;
    for (const auto& n : serialized["nodes"].get_array())
    {
        try
        {
            nodes.emplace_back(Node::Deserialize(local_id_generator(), local_id_generator, n));
            node_indices.push_back(num_nodes);
            num_nodes += 1;
        }
        catch (std::exception)
        {
            node_indices.push_back(-1);
        }
    }

    loading_error = false;
    for (const auto& l : serialized["links"].get_array())
    {
        const int start_node_index = l["start"]["node"].get<int>();
        const int end_node_index = l["end"]["node"].get<int>();
        // At least one of the linked node wasn't properly loaded
        if (start_node_index >= node_indices.size() || end_node_index >= node_indices.size() ||
            node_indices[start_node_index] == -1 || node_indices[end_node_index] == -1)
        {
            loading_error = true;
        }

        const Node* start_node = nodes[node_indices[start_node_index]].get();
        const Node* end_node = nodes[node_indices[end_node_index]].get();

        const int start_pin_index = l["start"]["pin"].get<int>();
        const int end_pin_index = l["end"]["pin"].get<int>();

        if (start_pin_index >= start_node->outs.size() || end_pin_index >= end_node->ins.size())
        {
            loading_error = true;
        }

        Pin* start = start_node->outs[start_pin_index].get();
        Pin* end = end_node->ins[end_pin_index].get();
        links.emplace_back(std::make_unique<Link>(local_id_generator(), start, end));
        start->link = links.back().get();
        end->link = links.back().get();
    }

    CreateInsOuts(id_generator);
    ComputePowerUsage();
    UpdateDetails();
}

Node::Kind GroupNode::GetKind() const
{
    return Kind::Group;
}

bool GroupNode::IsGroup() const
{
    return true;
}

Json::Value GroupNode::Serialize() const
{
    Json::Value node = PoweredNode::Serialize();
    node["name"] = name;

    Json::Array serialized_nodes;
    serialized_nodes.reserve(nodes.size());
    for (const auto& n : nodes)
    {
        serialized_nodes.push_back(n->Serialize());
    }
    node["nodes"] = serialized_nodes;

    auto get_node_index = [&](const Node* node) {
        for (int i = 0; i < nodes.size(); ++i)
        {
            if (nodes[i].get() == node)
            {
                return i;
            }
        }
        return -1;
    };

    auto get_pin_index = [&](const Pin* pin) {
        const std::vector<std::unique_ptr<Pin>>& pins = pin->direction == ax::NodeEditor::PinKind::Input ? pin->node->ins : pin->node->outs;
        for (int i = 0; i < pins.size(); ++i)
        {
            if (pins[i].get() == pin)
            {
                return i;
            }
        }
        return -1;
    };

    Json::Array serialized_links;
    serialized_links.reserve(links.size());
    for (const auto& l : links)
    {
        const int start_node_index = get_node_index(l->start->node);
        const int end_node_index = get_node_index(l->end->node);
        const int start_pin_index = get_pin_index(l->start);
        const int end_pin_index = get_pin_index(l->end);
        if (start_node_index == -1 || end_node_index == -1 ||
            start_pin_index == -1 || end_pin_index == -1)
        {
            continue;
        }

        serialized_links.push_back({
            { "start", {
                { "node", get_node_index(l->start->node) },
                { "pin", get_pin_index(l->start) }
            }},
            { "end", {
                { "node", get_node_index(l->end->node) },
                { "pin", get_pin_index(l->end) }
            }}
        });
    }
    node["links"] = serialized_links;

    return node;
}

void GroupNode::UpdateRate(const FractionalNumber& new_rate)
{
    current_rate = new_rate;

    PropagateRateToSubnodes();
    ComputePowerUsage();
    UpdateDetails();

    // Inputs and outputs should always be proportional to the current rate as the group acts as one big CraftNode (I think)
    for (auto& p : ins)
    {
        p->current_rate = p->base_rate * current_rate;
    }
    for (auto& p : outs)
    {
        p->current_rate = p->base_rate * current_rate;
    }
}

bool GroupNode::HasVariablePower() const
{
    return variable_power;
}

void GroupNode::ComputePowerUsage()
{
    same_clock_power = FractionalNumber(0, 1);
    last_underclock_power = FractionalNumber(0, 1);

    variable_power = false;
    for (auto& n : nodes)
    {
        if (n->IsPowered())
        {
            PoweredNode* powered = static_cast<CraftNode*>(n.get());
            powered->ComputePowerUsage();
            same_clock_power += powered->same_clock_power;
            last_underclock_power += powered->last_underclock_power;
            variable_power |= powered->HasVariablePower();
        }
    }
}

void GroupNode::PropagateRateToSubnodes()
{
    inputs.clear();
    outputs.clear();

    for (size_t i = 0; i < nodes.size(); ++i)
    {
        // Update powered node with current rate to get proper power
        if (nodes[i]->IsPowered())
        {
            static_cast<PoweredNode*>(nodes[i].get())->UpdateRate(nodes_base_rate[i] * current_rate);

            if (nodes[i]->IsCraft())
            {
                for (auto& p : nodes[i]->ins)
                {
                    inputs[p->item] += p->current_rate;
                }
                for (auto& p : nodes[i]->outs)
                {
                    outputs[p->item] += p->current_rate;
                }
            }
            else if (nodes[i]->IsGroup())
            {
                const GroupNode* node = static_cast<const GroupNode*>(nodes[i].get());
                for (const auto& [k, v] : node->inputs)
                {
                    inputs[k] += v;
                }
                for (const auto& [k, v] : node->outputs)
                {
                    outputs[k] += v;
                }
            }
        }
        else if (nodes[i]->IsSink())
        {
            for (auto& p : nodes[i]->ins)
            {
                if (p->item != nullptr)
                {
                    inputs[p->item] += p->current_rate * current_rate;
                }
            }
        }
        // Don't update organizer nodes so the current_rate actually stores the base_rate
        // (this prevents the information to be lost when group rate is set to 0)
    }
}

void GroupNode::SetBuiltState(const bool b)
{
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        if (nodes[i]->IsCraft())
        {
            static_cast<CraftNode*>(nodes[i].get())->built = b;
        }
        else if (nodes[i]->IsGroup())
        {
            static_cast<GroupNode*>(nodes[i].get())->SetBuiltState(b);
        }
    }
    UpdateDetails();
}

void GroupNode::CreateInsOuts(const std::function<unsigned long long int()>& id_generator)
{
    inputs.clear();
    outputs.clear();
    nodes_base_rate.reserve(nodes.size());
    for (auto& n : nodes)
    {
        if (n->IsCraft())
        {
            for (auto& p : n->ins)
            {
                inputs[p->item] += p->current_rate;
            }
            for (auto& p : n->outs)
            {
                outputs[p->item] += p->current_rate;
            }
        }
        else if (n->IsGroup())
        {
            const GroupNode* node = static_cast<const GroupNode*>(n.get());
            for (const auto& [k, v] : node->inputs)
            {
                inputs[k] += v;
            }
            for (const auto& [k, v] : node->outputs)
            {
                outputs[k] += v;
            }
        }
        // Add all sink inputs as required additional inputs
        else if (n->IsSink())
        {
            for (auto& p : n->ins)
            {
                if (p->item != nullptr)
                {
                    inputs[p->item] += p->current_rate;
                }
            }
        }

        if (n->IsPowered())
        {
            nodes_base_rate.push_back(static_cast<PoweredNode*>(n.get())->current_rate);
        }
        else
        {
            nodes_base_rate.push_back(FractionalNumber(0, 1));
        }
    }

    // Create input pins for resources required in the group
    for (const auto& [k, v] : inputs)
    {
        const auto it = outputs.find(k);
        // Only consumed, create an input pin
        if (it == outputs.end())
        {
            ins.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Input, this, k, v));
            ins.back()->current_rate = v;
        }
        // Less produced than consumed, create an input pin with the difference
        else if (it->second < v)
        {
            ins.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Input, this, k, v - it->second));
            ins.back()->current_rate = v - it->second;
        }
    }

    // Create output pins for resources overproduced in the group
    for (const auto& [k, v] : outputs)
    {
        const auto it = inputs.find(k);
        // Only produced, create an output pin
        if (it == inputs.end())
        {
            outs.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Output, this, k, v));
            outs.back()->current_rate = v;
        }
        // Less consumed than produced, create an output pin with the difference
        else if (it->second < v)
        {
            outs.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Output, this, k, v - it->second));
            outs.back()->current_rate = v - it->second;
        }
    }
}

void GroupNode::UpdateDetails()
{
    total_machines = {};
    built_machines = {};
    detailed_machines = {};
    detailed_power_same_clock = {};
    detailed_power_last_underclock = {};
    detailed_sinked_points = {};
    for (const auto& n : nodes)
    {
        if (n->IsCraft())
        {
            const CraftNode* node = static_cast<const CraftNode*>(n.get());
            total_machines[node->recipe->building->name] += node->current_rate;
            built_machines[node->recipe->building->name] += node->built ? node->current_rate : 0;
            detailed_machines[node->recipe->building->name][node->recipe] += node->current_rate;
            detailed_power_same_clock[node->recipe] += node->same_clock_power;
            detailed_power_last_underclock[node->recipe] += node->last_underclock_power;
        }
        else if (n->IsGroup())
        {
            const GroupNode* node = static_cast<const GroupNode*>(n.get());
            for (const auto& [k, v] : node->total_machines)
            {
                total_machines[k] += v;
            }
            for (const auto& [k, v] : node->built_machines)
            {
                built_machines[k] += v;
            }
            for (const auto& [k, v] : node->detailed_machines)
            {
                for (const auto& [k2, v2] : v)
                {
                    detailed_machines[k][k2] += v2;
                }
            }
            for (const auto& [k, v] : node->detailed_power_same_clock)
            {
                detailed_power_same_clock[k] += v;
            }
            for (const auto& [k, v] : node->detailed_power_last_underclock)
            {
                detailed_power_last_underclock[k] += v;
            }
        }
        else if (n->IsSink())
        {
            for (const auto& p : n->ins)
            {
                if (p->item != nullptr)
                {
                    detailed_sinked_points[p->item] = p->current_rate * current_rate * p->item->sink_value;
                }
            }
        }
    }
}

OrganizerNode::OrganizerNode(const ax::NodeEditor::NodeId id, const Item* item) : Node(id), item(item)
{

}

OrganizerNode::OrganizerNode(const ax::NodeEditor::NodeId id, const Json::Value& serialized) : Node(id, serialized), item(nullptr)
{
    const Kind kind = static_cast<Kind>(serialized["kind"].get<int>());
    if (kind != Kind::Merger && kind != Kind::CustomSplitter && kind != Kind::GameSplitter)
    {
        throw std::runtime_error("Trying to deserialize an unvalid node as an organizer node");
    }
    auto item_it = Data::Items().find(serialized["item"].get_string());
    if (item_it != Data::Items().end())
    {
        item = item_it->second.get();
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

CustomSplitterNode::CustomSplitterNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Item* item) : OrganizerNode(id, item)
{
    ins.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Input, this, item));
    outs.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Output, this, item));
    outs.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Output, this, item));
    outs.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Output, this, item));
}

CustomSplitterNode::CustomSplitterNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Json::Value& serialized) : OrganizerNode(id, serialized)
{
    if (static_cast<Kind>(serialized["kind"].get<int>()) != Kind::CustomSplitter)
    {
        throw std::runtime_error("Trying to deserialize an unvalid node as a custom splitter node");
    }

    if (serialized["ins"].size() != 1)
    {
        throw std::runtime_error("Trying to deserialize an unvalid custom splitter node (wrong number of inputs)");
    }
    ins.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Input, this, item));
    ins.back()->current_rate = FractionalNumber(serialized["ins"][0]["num"].get<long long int>(), serialized["ins"][0]["den"].get<long long int>());

    for (int i = 0; i < serialized["outs"].size(); ++i)
    {
        outs.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Output, this, item));
        outs.back()->current_rate = FractionalNumber(serialized["outs"][i]["num"].get<long long int>(), serialized["outs"][i]["den"].get<long long int>());
    }
}

CustomSplitterNode::~CustomSplitterNode()
{
}

bool CustomSplitterNode::IsCustomSplitter() const
{
    return true;
}

Node::Kind CustomSplitterNode::GetKind() const
{
    return Node::Kind::CustomSplitter;
}

MergerNode::MergerNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Item* item) : OrganizerNode(id, item)
{
    ins.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Input, this, item));
    ins.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Input, this, item));
    ins.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Input, this, item));
    outs.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Output, this, item));
}

MergerNode::MergerNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Json::Value& serialized) : OrganizerNode(id, serialized)
{
    if (static_cast<Kind>(serialized["kind"].get<int>()) != Kind::Merger)
    {
        throw std::runtime_error("Trying to deserialize an unvalid node as a merger node");
    }

    for (int i = 0; i < serialized["ins"].size(); ++i)
    {
        ins.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Input, this, item));
        ins.back()->current_rate = FractionalNumber(serialized["ins"][i]["num"].get<long long int>(), serialized["ins"][i]["den"].get<long long int>());
    }

    if (serialized["outs"].size() != 1)
    {
        throw std::runtime_error("Trying to deserialize an unvalid merger node (wrong number of outputs)");
    }
    outs.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Output, this, item));
    outs.back()->current_rate = FractionalNumber(serialized["outs"][0]["num"].get<long long int>(), serialized["outs"][0]["den"].get<long long int>());
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

GameSplitterNode::GameSplitterNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Item* item) : OrganizerNode(id, item)
{
    ins.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Input, this, item));
    outs.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Output, this, item));
    outs.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Output, this, item));
    outs.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Output, this, item));
}

GameSplitterNode::GameSplitterNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Json::Value& serialized) : OrganizerNode(id, serialized)
{
    if (static_cast<Kind>(serialized["kind"].get<int>()) != Kind::GameSplitter)
    {
        throw std::runtime_error("Trying to deserialize an unvalid node as a game splitter node");
    }

    if (serialized["ins"].size() != 1)
    {
        throw std::runtime_error("Trying to deserialize an unvalid game splitter node (wrong number of inputs)");
    }
    ins.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Input, this, item));
    ins.back()->current_rate = FractionalNumber(serialized["ins"][0]["num"].get<long long int>(), serialized["ins"][0]["den"].get<long long int>());

    for (int i = 0; i < serialized["outs"].size(); ++i)
    {
        outs.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Output, this, item));
        outs.back()->current_rate = FractionalNumber(serialized["outs"][i]["num"].get<long long int>(), serialized["outs"][i]["den"].get<long long int>());
    }
}

GameSplitterNode::~GameSplitterNode()
{
}

bool GameSplitterNode::IsGameSplitter() const
{
    return true;
}

Node::Kind GameSplitterNode::GetKind() const
{
    return Node::Kind::GameSplitter;
}

bool GameSplitterNode::IsBalanced() const
{
    if (outs.size() == 0)
    {
        return OrganizerNode::IsBalanced();
    }

    for (const auto& p : outs)
    {
        if (p->current_rate != outs[0]->current_rate)
        {
            return false;
        }
    }
    return OrganizerNode::IsBalanced();
}

SinkNode::SinkNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Item* item) : Node(id)
{
    ins.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Input, this, item));
}

SinkNode::SinkNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Json::Value& serialized) : Node(id, serialized)
{
    if (static_cast<Kind>(serialized["kind"].get<int>()) != Kind::Sink)
    {
        throw std::runtime_error("Trying to deserialize an unvalid node as a sink node");
    }

    if (serialized["ins"].size() == 0)
    {
        throw std::runtime_error("Trying to deserialize an unvalid sink node (wrong number of inputs)");
    }
    for (const auto& i : serialized["ins"].get_array())
    {
        const Item* item = nullptr;
        auto item_it = Data::Items().find(i["item"].get_string());
        if (item_it != Data::Items().end())
        {
            item = item_it->second.get();
        }
        else if (i["item"].get_string() != "")
        {
            throw std::runtime_error("Unknown item when loading sink node");
        }
        ins.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Input, this, item));
        ins.back()->current_rate = FractionalNumber(i["num"].get<long long int>(), i["den"].get<long long int>());
    }
}

SinkNode::~SinkNode()
{

}

bool SinkNode::IsSink() const
{
    return true;
}

Node::Kind SinkNode::GetKind() const
{
    return Node::Kind::Sink;
}

Json::Value SinkNode::Serialize() const
{
    Json::Value serialized = Node::Serialize();

    Json::Array ins_array;
    ins_array.reserve(ins.size());
    for (auto& i : ins)
    {
        ins_array.push_back({
            { "num", i->current_rate.GetNumerator() },
            { "den", i->current_rate.GetDenominator() },
            { "item",i->item == nullptr ? "" : i->item->name }
        });
    }
    serialized["ins"] = ins_array;

    return serialized;
}
