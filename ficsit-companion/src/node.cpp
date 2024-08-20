#include "node.hpp"
#include "pin.hpp"
#include "recipe.hpp"

#include <imgui_node_editor.h>

Node::Node(const ax::NodeEditor::NodeId id) : id(id)
{

}

Node::~Node()
{

}

CraftNode::CraftNode(const ax::NodeEditor::NodeId id, const Recipe* recipe, const std::function<unsigned long long int()>& id_generator) :
    Node(id), recipe(recipe), current_rate(1, 1)
{
    for (const auto& input : recipe->ins)
    {
        ins.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Input, this, input.item, input.quantity));
    }
    for (const auto& output : recipe->outs)
    {
        outs.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Output, this, output.item, output.quantity));
    }
}

CraftNode::~CraftNode()
{

}

Node::Kind CraftNode::GetKind() const
{
    return Node::Kind::Craft;
}

OrganizerNode::OrganizerNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator) : Node(id), item(nullptr)
{

}

OrganizerNode::~OrganizerNode()
{

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

SplitterNode::SplitterNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator) : OrganizerNode(id, id_generator)
{
    ins.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Input, this, nullptr));
    outs.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Output, this, nullptr));
    outs.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Output, this, nullptr));
}

SplitterNode::~SplitterNode()
{
}

Node::Kind SplitterNode::GetKind() const
{
    return Node::Kind::Splitter;
}

MergerNode::MergerNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator) : OrganizerNode(id, id_generator)
{
    ins.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Input, this, nullptr));
    ins.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Input, this, nullptr));
    outs.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Output, this, nullptr));
}

MergerNode::~MergerNode()
{

}

Node::Kind MergerNode::GetKind() const
{
    return Node::Kind::Merger;
}
