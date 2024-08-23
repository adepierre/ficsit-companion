#include "link.hpp"
#include "pin.hpp"

#include <imgui_node_editor.h>

#include <cassert>

Link::Link(const ax::NodeEditor::LinkId id, Pin* start, Pin* end) :
    id(id),
    start(start),
    end(end),
    // We check for nullptr because derferencing a nullptr is nasty and the assert will catch it with a better error message
    start_id(start == nullptr ? ax::NodeEditor::PinId::Invalid : start->id),
    end_id(end == nullptr ? ax::NodeEditor::PinId::Invalid : end->id)
{
    assert(start != nullptr && "start shouldn't be null in Link constructor");
    assert(end != nullptr && "end shouldn't be null in Link constructor");
    assert(start->direction == ax::NodeEditor::PinKind::Output && "start should be an output Pin in Link constructor");
    assert(end->direction == ax::NodeEditor::PinKind::Input && "end should be an input Pin in Link constructor");

    flow = std::optional<ax::NodeEditor::FlowDirection>();
}

Link::~Link()
{

}
