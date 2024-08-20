#include "link.hpp"
#include "pin.hpp"

#include <imgui_node_editor.h>

Link::Link(const ax::NodeEditor::LinkId id, Pin* start, Pin* end) : id(id), start(start), end(end),
    start_id(start == nullptr ? ax::NodeEditor::PinId::Invalid : start->id),
    end_id(end == nullptr ? ax::NodeEditor::PinId::Invalid : end->id)
{
    flow = std::optional<ax::NodeEditor::FlowDirection>();
}

Link::~Link()
{

}
