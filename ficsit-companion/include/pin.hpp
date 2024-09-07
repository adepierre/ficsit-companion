#pragma once

#include "fractional_number.hpp"

#include <imgui_node_editor.h>

struct Node;
struct Item;
struct Link;
struct CountedItem;
struct MetaNode;
struct MetaLink;

struct Pin
{
    Pin(const ax::NodeEditor::PinId id, const ax::NodeEditor::PinKind direction,
        Node* node, const Item* item, const FractionalNumber& base_rate = FractionalNumber(0,1));
    ~Pin();

    const ax::NodeEditor::PinId id;
    const ax::NodeEditor::PinKind direction;
    Node* node;
    const Item* item;
    const FractionalNumber base_rate;
    // A pin can have at most one link
    Link* link;
    bool locked;

    FractionalNumber current_rate;
};

struct MetaPin : public Pin
{
    MetaPin(const ax::NodeEditor::PinId id, const ax::NodeEditor::PinKind direction,
        MetaNode* node, Pin* pin, FractionalNumber& meta_rate = FractionalNumber(0, 1));
    ~MetaPin();

    FractionalNumber meta_rate;
    Pin* pin;
    MetaNode* node;
    MetaLink* link;
};
