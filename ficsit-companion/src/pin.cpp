#include "pin.hpp"
#include "node.hpp"
#include "recipe.hpp"

Pin::Pin(const ax::NodeEditor::PinId id, const ax::NodeEditor::PinKind direction,
    Node* node, const Item* item, const FractionalNumber& base_rate) :
    id(id), direction(direction), node(node), link(nullptr), item(item), base_rate(base_rate), current_rate(0, 1), locked(false)
{
    if (node->GetKind() == Node::Kind::Craft)
    {
        current_rate = static_cast<CraftNode*>(node)->current_rate * base_rate;
    }
}

Pin::~Pin()
{

}

MetaPin::MetaPin(const ax::NodeEditor::PinId id, const ax::NodeEditor::PinKind direction,
    MetaNode* node, Pin* pin, FractionalNumber& meta_rate) :
    Pin(id, direction, node, nullptr, meta_rate), meta_rate(meta_rate), pin(pin), link(nullptr), node(node)
{

}

MetaPin::~MetaPin()
{

}
