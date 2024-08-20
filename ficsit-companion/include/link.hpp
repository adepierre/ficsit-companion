#pragma once

#include <optional>

#include <imgui_node_editor.h>

struct Pin;

struct Link
{
    Link(const ax::NodeEditor::LinkId id, Pin* start, Pin* end);
    ~Link();

    const ax::NodeEditor::LinkId id;
    Pin* start;
    Pin* end;
    const ax::NodeEditor::PinId start_id;
    const ax::NodeEditor::PinId end_id;

    std::optional<ax::NodeEditor::FlowDirection> flow;
};
