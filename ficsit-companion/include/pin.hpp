#pragma once

#include "fractional_number.hpp"

#include <imgui_node_editor.h>

struct Node;
struct Item;
struct Link;
struct CountedItem;

struct Pin
{
    Pin(const ax::NodeEditor::PinId id, const ax::NodeEditor::PinKind direction,
        Node* node, const Item* item, const FractionalNumber& base_rate = FractionalNumber(0,1));
    ~Pin();
    void SetLocked(const bool b);
    bool GetLocked() const;

    const ax::NodeEditor::PinId id;
    const ax::NodeEditor::PinKind direction;
    Node* node;
    const Item* item;
    const FractionalNumber base_rate;
    // A pin can have at most one link
    Link* link;

    FractionalNumber current_rate;
    bool error;
private:
    bool locked;
};
