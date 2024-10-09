#include "app.hpp"
#include "building.hpp"
#include "fractional_number.hpp"
#include "game_data.hpp"
#include "json.hpp"
#include "link.hpp"
#include "node.hpp"
#include "pin.hpp"
#include "recipe.hpp"
#include "utils.hpp"

// For InputText with std::string
#include <misc/cpp/imgui_stdlib.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>

/// @brief Save text file (either on disk for desktop version or in localStorage for web version)
/// @param path
/// @param content
static void SaveFile(const std::string& path, const std::string& content)
{
#if defined(__EMSCRIPTEN__)
    EM_ASM({ localStorage.setItem(UTF8ToString($0), UTF8ToString($1)); }, path.c_str(), content.c_str());
#else
    std::ofstream f(path, std::ios::out);
    f << content;
    f.close();
#endif
}

/// @brief Load text file (either from disk for desktop version or in localStorage for web version)
/// @param path
/// @return std::nullopt if file does not exist, file content otherwise
static std::optional<std::string> LoadFile(const std::string& path)
{
#if !defined(__EMSCRIPTEN__)
    // Load file if it exists
    if (std::filesystem::exists(path))
    {
        std::ifstream f(path, std::ios::in);
        return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    return std::nullopt;
#else
    // Read from localStorage
    char* content_raw = static_cast<char*>(EM_ASM_PTR({
        var str = localStorage.getItem(UTF8ToString($0)) || "";
        var length = lengthBytesUTF8(str) + 1;
        var str_wasm = _malloc(length);
        stringToUTF8(str, str_wasm, length);
        return str_wasm;
    }, path.c_str()));
    const std::string content(content_raw);
    free(static_cast<void*>(content_raw));
    return content.empty() ? std::nullopt : std::optional<std::string>(content);
#endif
}

/// @brief Remove a file (either on disk for desktop version or in localStorage for web version)
/// @param path
static void RemoveFile(const std::string& path)
{
#if !defined(__EMSCRIPTEN__)
    if (std::filesystem::exists(path))
    {
        std::filesystem::remove(path);
    }
#else
    EM_ASM({
        localStorage.removeItem(UTF8ToString($0));
    }, path.c_str());
#endif
}

App::App()
{
    next_id = 1;
    last_time_saved_session = 0.0;

    config.SettingsFile = nullptr;
    config.EnableSmoothZoom = true;

    context = ax::NodeEditor::CreateEditor(&config);

    popup_opened = false;
    new_node_pin = nullptr;

    recipe_filter = "";

    somersloop_texture_id = LoadTextureFromFile("icons/Wat_1_64.png");

    last_time_interacted = std::chrono::steady_clock::now();

    LoadSettings();
}

App::~App()
{
    ax::NodeEditor::DestroyEditor(context);

    // Save current state
    // Destructor is not called in emscripten, we're using emscripten_set_beforeunload_callback in main.cpp instead
#if !defined(__EMSCRIPTEN__)
    SaveSession();
#endif
}


/******************************************************\
*             Non render related functions             *
\******************************************************/
void App::SaveSession()
{
    SaveFile(session_file.data(), Serialize());
}

bool App::HasRecentInteraction() const
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - last_time_interacted).count() < 10000;
}

void App::LoadSession()
{
    // Load session file if it exists
    const std::optional<std::string> content = LoadFile(session_file.data());
    if (!content.has_value())
    {
        return;
    }
    Deserialize(content.value());
}

void App::LoadSettings()
{
    const std::optional<std::string> content = LoadFile(settings_file.data());

    Json::Value json = content.has_value() ? Json::Parse(content.value()) : Json::Object();

    // Load all settings values from json
    settings.hide_spoilers = !json.contains("hide_spoilers") || json["hide_spoilers"].get<bool>(); // default true
    settings.hide_somersloop = !json.contains("hide_somersloop") || json["hide_somersloop"].get<bool>(); // default true
    settings.diff_in_out = json.contains("diff_in_out") && json["diff_in_out"].get<bool>(); // default false
    settings.unlocked_alts = {};

    for (const auto& r : Data::Recipes())
    {
        if (r->alternate)
        {
            settings.unlocked_alts[r.get()] = json.contains("unlocked_alts") && json["unlocked_alts"].contains(r->name.substr(1)) && json["unlocked_alts"][r->name.substr(1)].get<bool>();
        }
    }

    if (!content.has_value())
    {
        SaveSettings();
    }
}

void App::SaveSettings() const
{
    Json::Value serialized;

    // Save all settings values in the json
    serialized["hide_spoilers"] = settings.hide_spoilers;
    serialized["hide_somersloop"] = settings.hide_somersloop;
    serialized["diff_in_out"] = settings.diff_in_out;

    Json::Object unlocked;
    for (const auto& [r, b] : settings.unlocked_alts)
    {
        // Remove the leading "*" from the alt recipe name
        unlocked[r->name.substr(1)] = b;
    }
    serialized["unlocked_alts"] = unlocked;

    SaveFile(settings_file.data(), serialized.Dump());
}

std::string App::Serialize() const
{
    Json::Value output;
    output["save_version"] = SAVE_VERSION;
    output["game_version"] = Data::Version();

    Json::Array saved_nodes;
    saved_nodes.reserve(nodes.size());
    for (const auto& n : nodes)
    {
        saved_nodes.push_back(n->Serialize());
    }
    output["nodes"] = saved_nodes;


    auto get_node_index = [&](const Node* n) -> int {
        for (int i = 0; i < nodes.size(); ++i)
        {
            if (nodes[i].get() == n)
            {
                return i;
            }
        }
        return -1;
        };

    auto get_pin_index = [&](const Node* n, const Pin* p, const bool out) -> int {
        const std::vector<std::unique_ptr<Pin>>& pins = out ? n->outs : n->ins;
        for (int i = 0; i < pins.size(); ++i)
        {
            if (pins[i].get() == p)
            {
                return i;
            }
        }
        return -1;
        };

    Json::Array saved_links;
    saved_links.reserve(links.size());
    for (const auto& l : links)
    {
        saved_links.push_back({
            { "start", {
                { "node", get_node_index(l->start->node) },
                { "pin", get_pin_index(l->start->node, l->start, true) }
            }},
            { "end", {
                { "node", get_node_index(l->end->node) },
                { "pin", get_pin_index(l->end->node, l->end, false) }
            }}
        });
    }
    output["links"] = saved_links;

    return output.Dump();
}

void App::Deserialize(const std::string& s)
{
    Json::Value content = Json::Parse(s);
    if (content.is_null() || content.size() == 0)
    {
        return;
    }

    if (!UpdateSave(content, SAVE_VERSION))
    {
        printf("Save format not supported with this version (%i VS %i)", content["save_version"].get<int>(), SAVE_VERSION);
        return;
    }

    // Clean current content
    for (const auto& n : nodes)
    {
        ax::NodeEditor::DeleteNode(n->id);
    }
    nodes.clear();

    for (const auto& l : links)
    {
        ax::NodeEditor::DeleteLink(l->id);
    }
    links.clear();

    auto get_recipe = [&](const std::string& name) -> const Recipe* {
        for (const auto& r : Data::Recipes())
        {
            if (r->name == name)
            {
                return r.get();
            }
        }
        return nullptr;
    };

    // Load nodes
    std::vector<int> node_indices;
    node_indices.reserve(content["nodes"].size());
    size_t num_nodes = 0;
    for (const auto& n : content["nodes"].get_array())
    {
        const Node::Kind kind = static_cast<Node::Kind>(n["kind"].get<int>());
        switch (kind)
        {
        case Node::Kind::Craft:
        {
            const Recipe* recipe = get_recipe(n["recipe"].get_string());
            if (recipe == nullptr)
            {
                node_indices.push_back(-1);
                break;
            }
            nodes.emplace_back(std::make_unique<CraftNode>(GetNextId(), recipe, std::bind(&App::GetNextId, this)));
            if (!nodes.back()->Deserialize(n))
            {
                nodes.pop_back();
                node_indices.push_back(-1);
            }
            else
            {
                node_indices.push_back(num_nodes);
                num_nodes += 1;
            }
            break;
        }
        case Node::Kind::Merger:
        case Node::Kind::Splitter:
        {
            auto item_it = Data::Items().find(n["item"].get_string());
            const Item* item = item_it == Data::Items().end() ? nullptr : item_it->second.get();

            if (item == nullptr)
            {
                node_indices.push_back(-1);
                break;
            }

            if (kind == Node::Kind::Merger)
            {
                nodes.emplace_back(std::make_unique<MergerNode>(GetNextId(), std::bind(&App::GetNextId, this), item));
            }
            else
            {
                nodes.emplace_back(std::make_unique<SplitterNode>(GetNextId(), std::bind(&App::GetNextId, this), item));
            }

            if (!nodes.back()->Deserialize(n))
            {
                nodes.pop_back();
                node_indices.push_back(-1);
            }
            else
            {
                node_indices.push_back(num_nodes);
                num_nodes += 1;
            }

            break;
        }
        }
    }

    for (const auto& n : nodes)
    {
        ax::NodeEditor::SetNodePosition(n->id, n->pos);
    }

    // Load links
    for (const auto& l : content["links"].get_array())
    {
        const int start_node_index = l["start"]["node"].get<int>();
        const int end_node_index = l["end"]["node"].get<int>();
        // At least one of the linked node wasn't properly loaded
        if (start_node_index >= node_indices.size() || end_node_index >= node_indices.size() ||
            node_indices[start_node_index] == -1 || node_indices[end_node_index] == -1)
        {
            continue;
        }

        const Node* start_node = nodes[node_indices[start_node_index]].get();
        const Node* end_node = nodes[node_indices[end_node_index]].get();

        const int start_pin_index = l["start"]["pin"].get<int>();
        const int end_pin_index = l["end"]["pin"].get<int>();

        if (start_pin_index >= start_node->outs.size() || end_pin_index >= end_node->ins.size())
        {
            continue;
        }

        CreateLink(start_node->outs[start_pin_index].get(), end_node->ins[end_pin_index].get());
    }
}

unsigned long long int App::GetNextId()
{
    return next_id++;
}

Pin* App::FindPin(ax::NodeEditor::PinId id) const
{
    if (id == ax::NodeEditor::PinId::Invalid)
    {
        return nullptr;
    }

    for (const auto& n : nodes)
    {
        for (const auto& p : n->ins)
        {
            if (p->id == id)
            {
                return p.get();
            }
        }

        for (const auto& p : n->outs)
        {
            if (p->id == id)
            {
                return p.get();
            }
        }
    }

    return nullptr;
}

void App::CreateLink(Pin* start, Pin* end)
{
    links.emplace_back(std::make_unique<Link>(GetNextId(),
        start->direction == ax::NodeEditor::PinKind::Output ? start : end,
        end->direction == ax::NodeEditor::PinKind::Input ? end : start));
    start->link = links.back().get();
    end->link = links.back().get();
    if (start->node->GetKind() != Node::Kind::Craft)
    {
        if (OrganizerNode* organizer_node = static_cast<OrganizerNode*>(start->node); organizer_node->item == nullptr)
        {
            organizer_node->ChangeItem(end->item);
        }
    }
    if (end->node->GetKind() != Node::Kind::Craft)
    {
        if (OrganizerNode* organizer_node = static_cast<OrganizerNode*>(end->node); organizer_node->item == nullptr)
        {
            organizer_node->ChangeItem(start->item);
        }
    }
    updating_pins.push({ start, Constraint::Strong });
}

void App::DeleteLink(const ax::NodeEditor::LinkId id)
{
    ax::NodeEditor::DeleteLink(id);
    auto it = std::find_if(links.begin(), links.end(), [id](const std::unique_ptr<Link>& link) { return link->id == id; });
    if (it != links.end())
    {
        if (Pin* start = (*it)->start; start != nullptr)
        {
            start->link = nullptr;
            // If either end was an organizer node, check the name is still valid
            if (start->node->GetKind() != Node::Kind::Craft)
            {
                static_cast<OrganizerNode*>(start->node)->RemoveItemIfNotForced();
            }
        }
        if (Pin* end = (*it)->end; end != nullptr)
        {
            end->link = nullptr;
            // If either end was an organizer node, check the name is still valid
            if (end->node->GetKind() != Node::Kind::Craft)
            {
                static_cast<OrganizerNode*>(end->node)->RemoveItemIfNotForced();
            }
        }
        links.erase(it);
    }
}

void App::DeleteNode(const ax::NodeEditor::NodeId id)
{
    ax::NodeEditor::DeleteNode(id);
    const auto it = std::find_if(nodes.begin(), nodes.end(), [id](const std::unique_ptr<Node>& n) { return n->id == id; });
    if (it != nodes.end())
    {
        for (auto& p : (*it)->ins)
        {
            if (p->link != nullptr)
            {
                DeleteLink(p->link->id);
            }
        }
        for (auto& p : (*it)->outs)
        {
            if (p->link != nullptr)
            {
                DeleteLink(p->link->id);
            }
        }
        nodes.erase(it);
    }
}

void App::UpdateNodesRate()
{
    if (updating_pins.size() == 0)
    {
        return;
    }
    std::unordered_map<const Pin*, Constraint> updated_pins;
    std::unordered_map<const Pin*, size_t> updated_count;

    auto GetConstraint = [&](const Pin* p)
        {
            auto it = updated_pins.find(p);
            return it == updated_pins.end() ? Constraint::None : it->second;
        };

    while (!updating_pins.empty())
    {
        const auto [updating_pin, updating_constraint] = updating_pins.front();
        updating_pins.pop();
        updated_pins[updating_pin] = updating_constraint;
        updated_count[updating_pin] += 1;

        // Update node from this pin
        switch (const Node::Kind kind = updating_pin->node->GetKind())
        {
        case Node::Kind::Craft:
        {
            CraftNode* node = static_cast<CraftNode*>(updating_pin->node);
            if (updating_pin->direction == ax::NodeEditor::PinKind::Output)
            {
                node->current_rate = updating_pin->current_rate / (updating_pin->base_rate * (1 + node->num_somersloop * node->recipe->building->somersloop_mult));
            }
            else
            {
                node->current_rate = updating_pin->current_rate / updating_pin->base_rate;
            }
            for (auto& p : node->ins)
            {
                const Constraint constraint = GetConstraint(p.get());
                if (constraint == Constraint::Strong)
                {
                    continue;
                }

                const FractionalNumber new_rate = node->current_rate * p->base_rate;
                if (new_rate == p->current_rate)
                {
                    continue;
                }
                p->current_rate = new_rate;
                updated_pins[p.get()] = updating_constraint;

                // Don't need to push it if it's not linked to anything
                if (p->link != nullptr)
                {
                    updating_pins.push({ p.get(), updating_constraint });
                }
            }
            for (auto& p : node->outs)
            {
                const FractionalNumber new_rate = node->current_rate * p->base_rate * (1 + (node->num_somersloop * node->recipe->building->somersloop_mult));
                if (new_rate == p->current_rate)
                {
                    continue;
                }
                p->current_rate = new_rate;
                if (p->link != nullptr)
                {
                    updating_pins.push({ p.get(), Constraint::Strong });
                }
            }
            break;
        }
        case Node::Kind::Splitter:
        case Node::Kind::Merger:
        {
            Node* node = updating_pin->node;
            std::vector<std::unique_ptr<Pin>>& one_pin = kind == Node::Kind::Splitter ? node->ins : node->outs;
            std::vector<std::unique_ptr<Pin>>& multi_pin = kind == Node::Kind::Splitter ? node->outs : node->ins;
            // One of the "multi pin" side has been updated
            if ((kind == Node::Kind::Splitter && updating_pin->direction == ax::NodeEditor::PinKind::Output) ||
                (kind == Node::Kind::Merger && updating_pin->direction == ax::NodeEditor::PinKind::Input))
            {
                const Constraint constraint = GetConstraint(one_pin[0].get());
                // Easy case, just sum all "multi pin" and update "single pin" side with the new value
                if (constraint != Constraint::Strong)
                {
                    FractionalNumber new_rate = FractionalNumber(0, 1);
                    const Pin* other_output_pin = nullptr;
                    for (const auto& p : multi_pin)
                    {
                        new_rate += p->current_rate;
                        if (p.get() != updating_pin)
                        {
                            other_output_pin = p.get();
                        }
                    }
                    if (one_pin[0]->current_rate != new_rate)
                    {
                        one_pin[0]->current_rate = new_rate;
                        const Constraint other_constraint = GetConstraint(other_output_pin);
                        updating_pins.push({
                            one_pin[0].get(),
                            (updating_constraint == Constraint::Strong && other_constraint == Constraint::Strong) ? Constraint::Strong : Constraint::Weak
                        });
                    }
                }
                // We can't update "single pin", try to balance the node if there is a weaker constrained pin on the "multi pin" side
                else
                {
                    Pin* other_pin = multi_pin[0].get() == updating_pin ? multi_pin[1].get() : multi_pin[0].get();
                    Constraint other_constraint = GetConstraint(other_pin);
                    if (other_constraint < Constraint::Strong && other_pin->link == nullptr)
                    {
                        other_constraint = Constraint::None;
                    }
                    // Can't balance
                    if (other_constraint >= updating_constraint || one_pin[0]->current_rate < updating_pin->current_rate)
                    {
                        break;
                    }

                    const FractionalNumber new_rate = one_pin[0]->current_rate - updating_pin->current_rate;
                    if (other_pin->current_rate != new_rate)
                    {
                        other_pin->current_rate = one_pin[0]->current_rate - updating_pin->current_rate;
                        // If it's linked, propagate the update
                        if (other_pin->link != nullptr)
                        {
                            updating_pins.push({
                                other_pin,
                                updating_constraint == Constraint::Strong ? Constraint::Strong : Constraint::Weak
                                });
                        }
                    }
                }
            }
            // More complicated case, "one pin" side is updated, how do we split the items with all the other pins ?
            else
            {
                Constraint constraint_0 = GetConstraint(multi_pin[0].get());
                if (constraint_0 != Constraint::Strong && multi_pin[0]->link == nullptr)
                {
                    constraint_0 = Constraint::None;
                }
                Constraint constraint_1 = GetConstraint(multi_pin[1].get());
                if (constraint_1 != Constraint::Strong && multi_pin[0]->link == nullptr)
                {
                    constraint_1 = Constraint::None;
                }
                // If one of the "multi pins" has a stronger constraint, use the other one to adjust if possible
                if (constraint_0 > constraint_1 || constraint_1 > constraint_0)
                {
                    const int constrained_idx = constraint_0 > constraint_1 ? 0 : 1;
                    const int other_idx = 1 - constrained_idx;
                    if (updating_pin->current_rate > multi_pin[constrained_idx]->current_rate)
                    {
                        multi_pin[other_idx]->current_rate = updating_pin->current_rate - multi_pin[constrained_idx]->current_rate;
                        if (multi_pin[other_idx]->link != nullptr)
                        {
                            const Constraint stronger_constraint = constraint_0 > constraint_1 ? constraint_0 : constraint_1;
                            updating_pins.push({
                                multi_pin[other_idx].get(),
                                updating_constraint == Constraint::Strong && stronger_constraint == Constraint::Strong ? Constraint::Strong : Constraint::Weak
                                });
                        }
                    }
                }
                // Else if pins have both no or weak constraints then try to split the new rate keeping the same split ratio
                else if (constraint_0 == constraint_1 && constraint_0 < Constraint::Strong)
                {
                    const FractionalNumber sum = multi_pin[0]->current_rate + multi_pin[1]->current_rate;
                    for (int i = 0; i < multi_pin.size(); ++i)
                    {
                        const FractionalNumber new_rate = sum.GetNumerator() == 0 ? (updating_pin->current_rate / FractionalNumber(2, 1)) : ((multi_pin[i]->current_rate / sum) * updating_pin->current_rate);
                        if (multi_pin[i]->current_rate != new_rate)
                        {
                            multi_pin[i]->current_rate = new_rate;
                            updating_pins.push({ multi_pin[i].get(), Constraint::Weak });
                        }
                    }
                }
                // Else, we can't know how the split should be so do nothing and let the user adjust
            }
            break;
        }
        }

        // If no link connected to this pin
        if (updating_pin->link == nullptr)
        {
            continue;
        }
        Pin* updated_pin = updating_pin->direction == ax::NodeEditor::PinKind::Input ? updating_pin->link->start : updating_pin->link->end;

        // If rate already match new one (or if it's probably in an infinite cycle loop)
        if (updated_pin->current_rate == updating_pin->current_rate || updated_count[updated_pin] > 8)
        {
            continue;
        }

        // If already updated with strong constraint
        const Constraint constraint = GetConstraint(updated_pin);
        if (constraint == Constraint::Strong)
        {
            continue;
        }

        // If here, copy rate and schedule this pin to trigger updates too
        updating_pin->link->flow = updating_pin->direction == ax::NodeEditor::PinKind::Input ? ax::NodeEditor::FlowDirection::Backward : ax::NodeEditor::FlowDirection::Forward;
        updated_pin->current_rate = updating_pin->current_rate;
        updating_pins.push({ updated_pin, updating_constraint });
    }

    // Just in case we exited the loop prematurely
    while (!updating_pins.empty())
    {
        updating_pins.pop();
    }

    // Make sure all crafting nodes are still valid regarding their recipe
    for (auto& n : nodes)
    {
        if (n->GetKind() != Node::Kind::Craft)
        {
            continue;
        }

        CraftNode* node = static_cast<CraftNode*>(n.get());

        for (auto& p : n->ins)
        {
            p->current_rate = p->base_rate * node->current_rate;
        }
        for (auto& p : n->outs)
        {
            p->current_rate = p->base_rate * node->current_rate * (1 + (node->num_somersloop * node->recipe->building->somersloop_mult));
        }
    }
}

void App::NudgeNodes()
{
    // Don't nudge item if the add node popup is open (arrow keys are used for navigation in the dropdown menu)
    if (ImGui::IsPopupOpen(add_node_popup_id.data()))
    {
        return;
    }

    ImVec2 nudge{
        -1.0f * ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) + 1.0f * ImGui::IsKeyPressed(ImGuiKey_RightArrow, false),
        -1.0f * ImGui::IsKeyPressed(ImGuiKey_UpArrow, false) + 1.0f * ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)
    };

    if (nudge.x == 0.0f && nudge.y == 0.0f)
    {
        return;
    }

    for (const auto& n : nodes)
    {
        if (ax::NodeEditor::IsNodeSelected(n->id))
        {
            ax::NodeEditor::SetNodePosition(n->id, { n->pos.x + nudge.x, n->pos.y + nudge.y });
        }
    }
}

void App::PullNodesPosition()
{
    for (auto& n : nodes)
    {
        n->pos = ax::NodeEditor::GetNodePosition(n->id);
    }
}




/******************************************************\
*              Rendering related functions             *
\******************************************************/
void App::Render()
{
    ax::NodeEditor::SetCurrentEditor(context);
    ax::NodeEditor::PushStyleColor(ax::NodeEditor::StyleColor_Flow, ImColor(1.0f, 1.0f, 0.0f));
    ax::NodeEditor::PushStyleColor(ax::NodeEditor::StyleColor_FlowMarker, ImColor(1.0f, 1.0f, 0.0f));

    ImGui::BeginChild("#left_panel", ImVec2(0.2f * ImGui::GetWindowSize().x, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNavInputs);
    RenderLeftPanel();
    ImGui::EndChild();

    ImGui::SameLine();

    ax::NodeEditor::Begin("Graph", ImGui::GetContentRegionAvail());

    // First frame
    if (ImGui::IsWindowAppearing())
    {
        // Try to load session
        LoadSession();
        last_time_saved_session = ImGui::GetTime();
    }

    if (ImGui::GetTime() - last_time_saved_session > 30.0)
    {
        // We need to update last_time_saved_session here because SaveSession needs
        // to be callable even without a valid ImGui context
        SaveSession();
        last_time_saved_session = ImGui::GetTime();
    }

    DeleteNodesLinks();
    DragLink();

    NudgeNodes();
    RenderNodes();
    RenderLinks();

    AddNewNode();

    UpdateNodesRate();
    CustomKeyControl();

    ax::NodeEditor::End();
    ax::NodeEditor::PopStyleColor();
    ax::NodeEditor::PopStyleColor();

    // We manually copy the pos of each node at each frame to make
    // sure they are available for serialization
    PullNodesPosition();

    ax::NodeEditor::SetCurrentEditor(nullptr);

    // Render the tooltips after we exited the NodeEditor context so
    // we are in the main window coordinates system instead of the
    // one from the graph view
    RenderTooltips();
}

#if defined(__EMSCRIPTEN__)
EM_ASYNC_JS(void, waitForFileInput, (), {
    var fileReady = false;
    var input = document.createElement("input");
    input.type = "file";
    input.accept = ".fcs";
    input.onchange = async function(event) {
        var file = event.target.files[0];
        if (file) {
            var reader = new FileReader();
            reader.onload = function() {
                var data = new Uint8Array(reader.result);
                FS.writeFile("/_internal_load_file", data);
                fileReady = true;
            };
            reader.readAsArrayBuffer(file);
        }
        else { fileReady = true; }
    };
    input.addEventListener("cancel", (event) => { fileReady = true; });
    input.click();

    // Wait until the file is ready
    while (!fileReady) {
        await new Promise(resolve => setTimeout(resolve, 100));
    }
});
#endif

void App::RenderLeftPanel()
{
    ImGui::BeginDisabled(ImGui::IsPopupOpen("##ControlsPopup"));
    if (ImGui::Button("Show controls list"))
    {
        ImGui::OpenPopup("##ControlsPopup");
    }
    ImGui::EndDisabled();

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), 0, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopup("##ControlsPopup", ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_ChildWindow))
    {
        RenderControlsPopup();
    }

    // If web version, add an option to load from disk and download
#if defined(__EMSCRIPTEN__)
    ImGui::SameLine();
    if (ImGui::Button("Export"))
    {
        const std::string path = "production_chain.fcs";
        const std::string content = Serialize();
        EM_ASM({
            var filename = UTF8ToString($0);
            var content = UTF8ToString($1);
            var blob = new Blob([content], { type: "text/plain" });
            var link = document.createElement("a");
            link.href = URL.createObjectURL(blob);
            link.download = filename;
            document.body.appendChild(link);
            link.click();
            document.body.removeChild(link);
        }, path.c_str(), content.c_str());
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s", "Export current production chain to disk");
    }
    ImGui::SameLine();
    if (ImGui::Button("Import"))
    {
        waitForFileInput();
        if (std::filesystem::exists("_internal_load_file"))
        {
            std::ifstream f("_internal_load_file", std::ios::in);
            const std::string content = std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            f.close();
            Deserialize(content);
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s", "Import a production chain from disk");
    }
#endif

    const float save_load_buttons_width = ImGui::CalcTextSize("Save").x + ImGui::CalcTextSize("Load").x + ImGui::GetStyle().FramePadding.x * 4;
    const float input_text_width = ImGui::GetContentRegionAvail().x - save_load_buttons_width - ImGui::GetStyle().ItemSpacing.x * 2;

    ImGui::PushItemWidth(input_text_width);
    if (ImGui::InputTextWithHint("##save_text", "Name to save/load...", &save_name))
    {
        for (auto& [filename, match] : file_suggestions)
        {
            match = filename.find(save_name);
        }
    }
    ImGui::PopItemWidth();

    // Autocomplete with save present locally
    const bool save_name_active = ImGui::IsItemActive();
    if (ImGui::IsItemActivated())
    {
        ImGui::OpenPopup("##AutocompletePopup");
    }
    {
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y));
        ImGui::SetNextWindowSizeConstraints({ ImGui::GetItemRectSize().x , 0.0f }, { ImGui::GetItemRectSize().x, ImGui::GetTextLineHeightWithSpacing() * 10.0f });
        if (ImGui::BeginPopup("##AutocompletePopup", ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_ChildWindow))
        {
            if (ImGui::IsWindowAppearing())
            {
                file_suggestions.clear();
                // Retrieve existing saved files
#if !defined(__EMSCRIPTEN__)
                if (!std::filesystem::is_directory(save_folder))
                {
                    std::filesystem::create_directory(save_folder);
                }
                for (const auto& f : std::filesystem::recursive_directory_iterator(save_folder))
                {
                    if (f.is_regular_file())
                    {
                        std::string path = f.path().string();
                        path = path.substr(0, path.size() - 4);
                        path = path.substr(save_folder.size() + 1);
                        file_suggestions.emplace_back(path, path.find(save_name));
                    }
                }
#else
                int names_size = 0;
                // Get all existing keys in localStorage starting with save_folder
                // return a pointer to string array and save array length in names_size
                // TODO/CHECK: are pointers always guaranteed to be 32 bits?
                char** names = static_cast<char**>(EM_ASM_PTR({
                    const keys = Object.keys(localStorage).filter(k => k.startsWith(UTF8ToString($0)));
                    var length = keys.length;
                    var buffer = _malloc(length * 4);
                    for (var i = 0; i < length; ++i)
                    {
                        var key = keys[i];
                        var key_length = lengthBytesUTF8(key) + 1;
                        var key_ptr = _malloc(key_length + 1);
                        stringToUTF8(key, key_ptr, key_length);
                        setValue(buffer + i * 4, key_ptr, "i32");
                    }
                    setValue($1, length, "i32");
                    return buffer;
                }, save_folder.data(), &names_size));

                for (int i = 0; i < names_size; ++i)
                {
                    std::string filename = std::string(names[i]).substr(save_folder.size() + 1);
                    filename = filename.substr(0, filename.size() - 4);
                    file_suggestions.emplace_back(filename, filename.find(save_name));
                    free(static_cast<void*>(names[i]));
                }
                free(static_cast<void*>(names));
#endif
            }

            if (file_suggestions.size() == 0)
            {
                ImGui::CloseCurrentPopup();
            }

            std::stable_sort(file_suggestions.begin(), file_suggestions.end(), [](const std::pair<std::string, size_t>& a, const std::pair<std::string, size_t>& b) { return a.second < b.second; });

            std::vector<std::string> removed;
            for (const auto& s : file_suggestions)
            {
                ImGui::PushID(s.first.c_str());
                if (ImGui::Button("X"))
                {
                    removed.push_back(s.first);
                }
                ImGui::PopID();
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", "Delete this file");
                }
                ImGui::SameLine();
                if (ImGui::Selectable(s.first.c_str()))
                {
                    save_name = s.first;
                    ImGui::CloseCurrentPopup();
                }
                // Add tooltip for long names
                if (ImGui::IsItemHovered() && ImGui::CalcTextSize(s.first.c_str()).x > ImGui::GetWindowWidth())
                {
                    ImGui::SetTooltip("%s", s.first.c_str());
                }
            }

            // Delete files that have been flagged by clicking on the button
            for (const auto& s : removed)
            {
                file_suggestions.erase(std::remove_if(file_suggestions.begin(), file_suggestions.end(),
                    [&](const std::pair<std::string, size_t>& p) {
                        return p.first == s;
                    }), file_suggestions.end());
                RemoveFile(std::string(save_folder) + "/" + s + ".fcs");
            }

            if (!save_name_active && !ImGui::IsWindowFocused())
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(save_name.empty());
    if (ImGui::Button("Save"))
    {
        // Save current state using provided name
        SaveFile(std::string(save_folder) + "/" + save_name + ".fcs", Serialize());
        save_name = "";
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s", "Save current production chain");
    }
    ImGui::SameLine();

    ImGui::BeginDisabled(file_suggestions.end() == std::find_if(file_suggestions.begin(), file_suggestions.end(), [&](const std::pair<std::string, size_t>& p) { return p.first == save_name; }));
    if (ImGui::Button("Load"))
    {
        const std::optional<std::string> content = LoadFile(std::string(save_folder) + "/" + save_name + ".fcs");
        if (content.has_value())
        {
            Deserialize(content.value());
        }
        save_name = "";
    }
    ImGui::EndDisabled();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s", "Load current production chain");
    }

    ImGui::SeparatorText("Settings");
    // Display all settings here
    if (ImGui::Checkbox("Hide 1.0 tier 5 spoiler", &settings.hide_somersloop))
    {
        SaveSettings();
    }
    if (ImGui::Checkbox("Hide 1.0 new advanced recipes", &settings.hide_spoilers))
    {
        SaveSettings();
    }
    if (ImGui::Checkbox("Compute Inputs/Outputs diff", &settings.diff_in_out))
    {
        SaveSettings();
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", "If enabled, common part of items in both inputs and outputs will be moved to intermediates (even if they are not linked)");
    }

    if (ImGui::Button("Unlock all alt recipes"))
    {
        settings.unlocked_alts = {};
        for (const auto& r : Data::Recipes())
        {
            if (r->alternate)
            {
                settings.unlocked_alts[r.get()] = true;
            }
        }
        SaveSettings();
    }
    if (ImGui::GetContentRegionAvail().x - ImGui::GetItemRectSize().x > ImGui::CalcTextSize("Reset alt recipes").x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x)
    {
        ImGui::SameLine();
    }
    if (ImGui::Button("Reset alt recipes"))
    {
        settings.unlocked_alts = {};
        for (const auto& r : Data::Recipes())
        {
            if (r->alternate)
            {
                settings.unlocked_alts[r.get()] = false;
            }
        }
        SaveSettings();
    }

    struct ItemPtrCompare {
        bool operator()(const Item* a, const Item* b) const { return a != nullptr && (b != nullptr && a->name < b->name); }
    };

    struct RecipePtrCompare {
        bool operator()(const Recipe* a, const Recipe* b) const { return a != nullptr && (b != nullptr && a->name < b->name);}
    };

    std::map<const Item*, FractionalNumber, ItemPtrCompare> inputs;
    std::map<const Item*, FractionalNumber, ItemPtrCompare> outputs;
    std::map<const Item*, FractionalNumber, ItemPtrCompare> intermediates;
    std::map<std::string, FractionalNumber> total_machines;
    std::map<std::string, std::map<const Recipe*, FractionalNumber, RecipePtrCompare>> detailed_machines;

    // Gather all inputs/outputs/machines
    for (const auto& n : nodes)
    {
        for (const auto& p : n->ins)
        {
            if (p->link == nullptr && p->item != nullptr)
            {
                inputs[p->item] += p->current_rate;
            }
        }
        for (const auto& p : n->outs)
        {
            if (p->link == nullptr && p->item != nullptr)
            {
                outputs[p->item] += p->current_rate;
            }
            else if (p->link != nullptr && p->item != nullptr && p->link->end->node->IsCraft())
            {
                intermediates[p->item] += p->current_rate;
            }
        }

        if (n->IsCraft())
        {
            const CraftNode* node = static_cast<const CraftNode*>(n.get());
            total_machines[node->recipe->building->name] += node->current_rate;
            detailed_machines[node->recipe->building->name][node->recipe] += node->current_rate;
        }
    }

    std::map<std::string, int> min_number_machines;
    for (const auto& [machine, map] : detailed_machines)
    {
        for (const auto& [r, n] : map)
        {
            min_number_machines[machine] += static_cast<int>(std::ceil(n.GetValue()));
        }
    }

    const float rate_width = ImGui::CalcTextSize("0000.000").x;

    ImGui::SeparatorText("Machines");
    for (auto& [machine, n] : total_machines)
    {
        if (n.GetNumerator() == 0)
        {
            continue;
        }
        // No visible color change when hovered/click
        ImGui::PushStyleColor(ImGuiCol_::ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_::ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
        const bool display_details = ImGui::TreeNodeEx(("##" + machine).c_str(), ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth);
        ImGui::PopStyleColor();
        ImGui::PopStyleColor();

        // Displayed over the TreeNodeEx element (same line)
        ImGui::SameLine();
        ImGui::SetNextItemWidth(rate_width);
        ImGui::BeginDisabled();
        ImGui::InputText("##rate", &n.GetStringFloat(), ImGuiInputTextFlags_ReadOnly);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("%s", n.GetStringFraction().c_str());
        }
        ImGui::SameLine();
        ImGui::Text("(%i)", min_number_machines[machine]);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", "Minimum number of machines at 100%");
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(machine.c_str());

        // Detailed list of recipes if the tree node is open
        if (display_details)
        {
            ImGui::Indent();
            for (auto& [recipe, n2] : detailed_machines[machine])
            {
                ImGui::SetNextItemWidth(rate_width);
                ImGui::BeginDisabled();
                ImGui::InputText("##rate", &n2.GetStringFloat(), ImGuiInputTextFlags_ReadOnly);
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    ImGui::SetTooltip("%s", n2.GetStringFraction().c_str());
                }
                ImGui::SameLine();
                ImGui::Text("(%i)", static_cast<int>(std::ceil(n2.GetValue())));
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", "Minimum number of machines at 100%");
                }
                ImGui::SameLine();

                recipe->Render();
            }

            ImGui::Unindent();

            ImGui::TreePop();
        }
    }

    ImGui::SeparatorText("Inputs");
    for (auto& [item, n] : inputs)
    {
        if (n.GetNumerator() == 0)
        {
            continue;
        }
        if (settings.diff_in_out)
        {
            if (const auto out_it = outputs.find(item); out_it != outputs.end())
            {
                // More output than input, skip this
                if (out_it->second > n)
                {
                    continue;
                }
                // Equal, just add it to intermediate
                else if (out_it->second == n)
                {
                    outputs.erase(out_it);
                    intermediates[item] += n;
                    continue;
                }
                // More input than output, display the diff and add the common part to intermediate
                else
                {
                    n = n - out_it->second;
                    intermediates[item] += out_it->second;
                    outputs.erase(out_it);
                }
            }
        }
        ImGui::SetNextItemWidth(rate_width);
        ImGui::BeginDisabled();
        ImGui::InputText("##rate", &n.GetStringFloat(), ImGuiInputTextFlags_ReadOnly);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("%s", n.GetStringFraction().c_str());
        }
        ImGui::SameLine();
        ImGui::Image((void*)(intptr_t)item->icon_gl_index, ImVec2(ImGui::GetTextLineHeightWithSpacing(), ImGui::GetTextLineHeightWithSpacing()));
        ImGui::SameLine();
        ImGui::TextUnformatted(item->name.c_str());
    }

    ImGui::SeparatorText("Outputs");
    for (auto& [item, n] : outputs)
    {
        if (n.GetNumerator() == 0)
        {
            continue;
        }
        if (settings.diff_in_out)
        {
            if (const auto in_it = inputs.find(item); in_it != inputs.end())
            {
                // More input than output, skip this
                if (in_it->second > n)
                {
                    continue;
                }
                // Equal should not happen as it's removed from output in input loop
                // More output than input, display the diff and add the common part to intermediate
                else
                {
                    n = n - in_it->second;
                    intermediates[item] += in_it->second;
                    inputs.erase(in_it);
                }
            }
        }
        ImGui::SetNextItemWidth(rate_width);
        ImGui::BeginDisabled();
        ImGui::InputText("##rate", &n.GetStringFloat(), ImGuiInputTextFlags_ReadOnly);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("%s", n.GetStringFraction().c_str());
        }
        ImGui::SameLine();
        ImGui::Image((void*)(intptr_t)item->icon_gl_index, ImVec2(ImGui::GetTextLineHeightWithSpacing(), ImGui::GetTextLineHeightWithSpacing()));
        ImGui::SameLine();
        ImGui::TextUnformatted(item->name.c_str());
    }

    ImGui::SeparatorText("Intermediates");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", "Items both produced and consumed in the production chain");
    }
    for (auto& [item, n] : intermediates)
    {
        if (n.GetNumerator() == 0)
        {
            continue;
        }
        ImGui::SetNextItemWidth(rate_width);
        ImGui::BeginDisabled();
        ImGui::InputText("##rate", &n.GetStringFloat(), ImGuiInputTextFlags_ReadOnly);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("%s", n.GetStringFraction().c_str());
        }
        ImGui::SameLine();
        ImGui::Image((void*)(intptr_t)item->icon_gl_index, ImVec2(ImGui::GetTextLineHeightWithSpacing(), ImGui::GetTextLineHeightWithSpacing()));
        ImGui::SameLine();
        ImGui::TextUnformatted(item->name.c_str());
    }
}

void App::RenderNodes()
{
    const float rate_width = ImGui::CalcTextSize("0000.000").x;
    const float somersloop_width = ImGui::CalcTextSize("000").x;
    // Vector that will be reused to sort pins for all nodes (instead of creating two per nodes)
    std::vector<size_t> sorted_pin_indices(4);
    auto sort_pin_indices = [&](const std::vector<std::unique_ptr<Pin>>& pins) {
        // Make sure there is enough elements in the vector
        const size_t N = pins.size();
        if (N > sorted_pin_indices.size())
        {
            sorted_pin_indices.resize(N);
        }
        for (size_t i = 0; i < N; ++i)
        {
            sorted_pin_indices[i] = i;
        }
        // Don't need to use stable sort as pins don't usually have the exact same Y coordinates
        std::sort(sorted_pin_indices.begin(), sorted_pin_indices.begin() + N, [&](const size_t i1, const size_t i2) {
            const std::unique_ptr<Pin>& p1 = pins[i1];
            const std::unique_ptr<Pin>& p2 = pins[i2];
            const Link* l1 = p1->link;
            const Link* l2 = p2->link;
            // Place pin with link above the others, and if multiple pin have a link, sort by linked node Y position
            return l1 != nullptr && (
                l2 == nullptr ||
                (p1->direction == ax::NodeEditor::PinKind::Input && l1->start->node->pos.y < l2->start->node->pos.y) ||
                (p1->direction == ax::NodeEditor::PinKind::Output && l1->end->node->pos.y < l2->end->node->pos.y)
            );
        });
    };

    for (const auto& node : nodes)
    {
        const bool isnt_balanced = node->IsOrganizer() && !static_cast<OrganizerNode*>(node.get())->IsBalanced();
        if (isnt_balanced)
        {
            ax::NodeEditor::PushStyleColor(ax::NodeEditor::StyleColor_NodeBorder, ImColor(255, 0, 0));
        }
        ax::NodeEditor::BeginNode(node->id);
        ImGui::PushID(node->id.AsPointer());
        ImGui::BeginVertical("node");
        {
            ImGui::BeginHorizontal("header");
            {
                switch (node->GetKind())
                {
                case Node::Kind::Craft:
                    ImGui::TextUnformatted(static_cast<CraftNode*>(node.get())->recipe->display_name.c_str());
                    break;
                case Node::Kind::Merger:
                    ImGui::TextUnformatted("Merger");
                    break;
                case Node::Kind::Splitter:
                    ImGui::TextUnformatted("Splitter");
                    break;
                }
            }
            ImGui::EndHorizontal();

            // spacing between header and content
            ImGui::Spring(0, ImGui::GetStyle().ItemSpacing.y * 2.0f);

            ImGui::BeginHorizontal("content");
            {
                ImGui::Spring(0.0f, 0.0f);
                ImGui::BeginVertical("inputs", ImVec2(0, 0), 0.0f); // Align elements on the left of the column
                {
                    // Set where the link will connect to (left center)
                    ax::NodeEditor::PushStyleVar(ax::NodeEditor::StyleVar_PivotAlignment, ImVec2(0, 0.5f));
                    ax::NodeEditor::PushStyleVar(ax::NodeEditor::StyleVar_PivotSize, ImVec2(0, 0));

                    sort_pin_indices(node->ins);
                    for (int idx = 0; idx < node->ins.size(); ++idx)
                    {
                        const auto& p = node->ins[sorted_pin_indices[idx]];
                        ax::NodeEditor::BeginPin(p->id, p->direction);
                        ImGui::BeginHorizontal(p->id.AsPointer());
                        {
                            const float radius = 0.2f * ImGui::GetTextLineHeightWithSpacing();
                            const ImVec2 size(2.0f * radius, 2.0f * radius);
                            // Draw circle
                            if (ImGui::IsRectVisible(size))
                            {
                                const ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
                                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                                const ImVec2 center(
                                    cursor_pos.x + radius,
                                    cursor_pos.y + radius
                                );
                                if (p->link == nullptr)
                                {
                                    draw_list->AddCircle(center, radius, ImColor(1.0f, 1.0f, 1.0f));
                                }
                                else
                                {
                                    draw_list->AddCircleFilled(center, radius, ImColor(1.0f, 1.0f, 1.0f));
                                }
                            }
                            ImGui::Dummy(size);
                            ImGui::Spring(0.0f);
                            ImGui::SetNextItemWidth(rate_width);
                            ImGui::InputText("##rate", &p->current_rate.GetStringFloat(), ImGuiInputTextFlags_CharsDecimal);
                            if (ImGui::IsItemDeactivatedAfterEdit())
                            {
                                try
                                {
                                    p->current_rate = FractionalNumber(p->current_rate.GetStringFloat());
                                    updating_pins.push({ p.get(), Constraint::Strong });
                                }
                                catch (const std::domain_error&)
                                {
                                    p->current_rate = FractionalNumber(p->current_rate.GetNumerator(), p->current_rate.GetDenominator());
                                }
                            }
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                            {
                                frame_tooltips.push_back(p->current_rate.GetStringFraction());
                            }

                            if (node->IsCraft())
                            {
                                ImGui::Spring(0.0f);
                                ImGui::Image((void*)(intptr_t)p->item->icon_gl_index, ImVec2(ImGui::GetTextLineHeightWithSpacing(), ImGui::GetTextLineHeightWithSpacing()));
                                ImGui::Spring(0.0f);
                                ImGui::TextUnformatted(p->item->new_line_name.c_str());
                                ImGui::Spring(0.0f);
                            }
                        }
                        ImGui::EndHorizontal();
                        ax::NodeEditor::EndPin();

                        ImGui::Spring(0.0f);
                    }
                    ax::NodeEditor::PopStyleVar();
                    ax::NodeEditor::PopStyleVar();
                    ImGui::Spring(1.0f, 0.0f);
                }
                ImGui::EndVertical();

                ImGui::Spring(1.0f);

                ImGui::BeginVertical("outputs", ImVec2(0, 0), 1.0f); // Align all elements on the right of the column
                {
                    // Set where the link will connect to (right center)
                    ax::NodeEditor::PushStyleVar(ax::NodeEditor::StyleVar_PivotAlignment, ImVec2(1.0f, 0.5f));
                    ax::NodeEditor::PushStyleVar(ax::NodeEditor::StyleVar_PivotSize, ImVec2(0, 0));
                    sort_pin_indices(node->outs);
                    for (int idx = 0; idx < node->outs.size(); ++idx)
                    {
                        const auto& p = node->outs[sorted_pin_indices[idx]];
                        ax::NodeEditor::BeginPin(p->id, p->direction);
                        ImGui::BeginHorizontal(p->id.AsPointer());
                        {
                            if (node->IsCraft())
                            {
                                ImGui::Spring(0.0f);
                                ImGui::TextUnformatted(p->item->new_line_name.c_str());
                                ImGui::Spring(0.0f);
                                ImGui::Image((void*)(intptr_t)p->item->icon_gl_index, ImVec2(ImGui::GetTextLineHeightWithSpacing(), ImGui::GetTextLineHeightWithSpacing()));
                            }
                            ImGui::Spring(0.0f);
                            ImGui::SetNextItemWidth(rate_width);
                            ImGui::InputText("##rate", &p->current_rate.GetStringFloat(), ImGuiInputTextFlags_CharsDecimal);
                            if (ImGui::IsItemDeactivatedAfterEdit())
                            {
                                try
                                {
                                    p->current_rate = FractionalNumber(p->current_rate.GetStringFloat());
                                    updating_pins.push({ p.get(), Constraint::Strong });
                                }
                                catch (const std::domain_error&)
                                {
                                    p->current_rate = FractionalNumber(p->current_rate.GetNumerator(), p->current_rate.GetDenominator());
                                }
                            }
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                            {
                                frame_tooltips.push_back(p->current_rate.GetStringFraction());
                            }
                            ImGui::Spring(0.0f);
                            const float radius = 0.2f * ImGui::GetTextLineHeightWithSpacing();
                            const ImVec2 size(2.0f * radius, 2.0f * radius);
                            // Draw circle
                            if (ImGui::IsRectVisible(size))
                            {
                                const ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
                                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                                const ImVec2 center(
                                    cursor_pos.x + radius,
                                    cursor_pos.y + radius
                                );
                                if (p->link == nullptr)
                                {
                                    draw_list->AddCircle(center, radius, ImColor(1.0f, 1.0f, 1.0f));
                                }
                                else
                                {
                                    draw_list->AddCircleFilled(center, radius, ImColor(1.0f, 1.0f, 1.0f));
                                }
                            }
                            ImGui::Dummy(size);
                        }
                        ImGui::EndHorizontal();
                        ax::NodeEditor::EndPin();

                        ImGui::Spring(0.0f);
                    }
                    ax::NodeEditor::PopStyleVar();
                    ax::NodeEditor::PopStyleVar();
                    ImGui::Spring(1.0f, 0.0f);
                }
                ImGui::EndVertical();
            }
            ImGui::EndHorizontal();

            ImGui::BeginHorizontal("bottom");
            {
                ImGui::Spring(1.0f);
                ImGui::SetNextItemWidth(rate_width);
                if (node->IsCraft())
                {
                    CraftNode* craft_node = static_cast<CraftNode*>(node.get());
                    ImGui::InputText("##rate", &craft_node->current_rate.GetStringFloat(), ImGuiInputTextFlags_CharsDecimal);
                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        try
                        {
                            craft_node->current_rate = FractionalNumber(craft_node->current_rate.GetStringFloat());
                            for (auto& p : craft_node->ins)
                            {
                                p->current_rate = p->base_rate * craft_node->current_rate;
                                updating_pins.push({ p.get(), Constraint::Strong });
                            }
                            for (auto& p : craft_node->outs)
                            {
                                p->current_rate = p->base_rate * craft_node->current_rate * (1 + (craft_node->num_somersloop * craft_node->recipe->building->somersloop_mult));
                                updating_pins.push({ p.get(), Constraint::Strong });
                            }
                        }
                        catch (const std::domain_error&)
                        {
                            craft_node->current_rate = FractionalNumber(craft_node->current_rate.GetNumerator(), craft_node->current_rate.GetDenominator());
                        }
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    {
                        frame_tooltips.push_back(craft_node->current_rate.GetStringFraction());
                    }
                    ImGui::Spring(0.0f);
                    ImGui::TextUnformatted(craft_node->recipe->building->name.c_str());
                    if (
                        // Override settings if it's not 0 (for example if the production chain is imported)
                        (settings.hide_somersloop && craft_node->num_somersloop.GetNumerator() == 0) ||
                        // Don't display somersloop if this building can't have one
                        craft_node->recipe->building->somersloop_mult.GetNumerator() == 0
                    )
                    {
                        ImGui::Spring(1.0f);
                    }
                    else
                    {
                        ImGui::Spring(1.0f);
                        ImGui::SetNextItemWidth(somersloop_width);
                        ImGui::InputText("##somersloop", &craft_node->num_somersloop.GetStringFraction(), ImGuiInputTextFlags_CharsDecimal);
                        if (ImGui::IsItemDeactivatedAfterEdit())
                        {
                            try
                            {
                                FractionalNumber new_num_somersloop = FractionalNumber(craft_node->num_somersloop.GetStringFraction());
                                // Only integer somersloop allowed
                                if (new_num_somersloop.GetDenominator() != 1)
                                {
                                    throw std::domain_error("somersloop num can only be whole integers");
                                }
                                // Check we don't try to boost more than 2x
                                // We know numerator is > 0 as otherwise somersloop input is not displayed, so it's ok to invert the fraction
                                if (new_num_somersloop > 1 / craft_node->recipe->building->somersloop_mult)
                                {
                                    new_num_somersloop = 1 / craft_node->recipe->building->somersloop_mult;
                                }
                                craft_node->num_somersloop = new_num_somersloop;
                                for (auto& p : craft_node->outs)
                                {
                                    p->current_rate = p->base_rate * craft_node->current_rate * (1 + (craft_node->num_somersloop * craft_node->recipe->building->somersloop_mult));
                                    updating_pins.push({ p.get(), Constraint::Strong });
                                }
                            }
                            catch (const std::domain_error&)
                            {
                                craft_node->num_somersloop = FractionalNumber(craft_node->num_somersloop.GetNumerator(), 1);
                            }
                        }
                        ImGui::Spring(0.0f);
                        ImGui::Image((void*)(intptr_t)somersloop_texture_id, ImVec2(ImGui::GetTextLineHeightWithSpacing(), ImGui::GetTextLineHeightWithSpacing()));
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        {
                            frame_tooltips.push_back("Alien Production Amplification");
                        }
                        ImGui::Spring(0.0f);
                    }
                }
                else
                {
                    OrganizerNode* org_node = static_cast<OrganizerNode*>(node.get());
                    if (org_node->item != nullptr)
                    {
                        ImGui::Spring(0.0f);
                        ImGui::TextUnformatted(org_node->item->name.c_str());
                        ImGui::Spring(0.0f);
                        ImGui::Image((void*)(intptr_t)org_node->item->icon_gl_index, ImVec2(ImGui::GetTextLineHeightWithSpacing(), ImGui::GetTextLineHeightWithSpacing()));
                        ImGui::Spring(0.0f);
                    }
                    ImGui::Spring(1.0f);
                }
            }
            ImGui::EndHorizontal();
        }
        ImGui::EndVertical();
        ImGui::PopID();
        ax::NodeEditor::EndNode();
        if (isnt_balanced)
        {
            ax::NodeEditor::PopStyleColor();
        }
    }
}

void App::RenderLinks()
{
    for (const auto& link : links)
    {
        ax::NodeEditor::Link(link->id, link->start_id, link->end_id, link->start->current_rate == link->end->current_rate ? ImColor(0.0f, 1.0f, 0.0f) : ImColor(1.0f, 0.0f, 0.0f));
        if (link->flow.has_value())
        {
            ax::NodeEditor::Flow(link->id, link->flow.value());
            link->flow = std::optional<ax::NodeEditor::FlowDirection>();
        }
    }
}

void App::DragLink()
{
    if (ax::NodeEditor::BeginCreate())
    {
        ax::NodeEditor::PinId input_pin_id = 0, output_pin_id = 0;
        if (ax::NodeEditor::QueryNewLink(&input_pin_id, &output_pin_id))
        {
            if (input_pin_id && output_pin_id)
            {
                Pin* start_pin = FindPin(input_pin_id);
                Pin* end_pin = FindPin(output_pin_id);
                if (start_pin == nullptr ||
                    end_pin == nullptr ||
                    start_pin == end_pin ||
                    start_pin->direction == end_pin->direction ||
                    start_pin->node == end_pin->node ||
                    start_pin->link != nullptr ||
                    end_pin->link != nullptr ||
                    (start_pin->item != nullptr && end_pin->item != nullptr && start_pin->item != end_pin->item)
                )
                {
                    ax::NodeEditor::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                }
                else if (ax::NodeEditor::AcceptNewItem(ImColor(128, 255, 128), 4.0f))
                {
                    // If we are dragging from an unset merger/setter, pull value instead of pushing it
                    if (start_pin->item == nullptr)
                    {
                        CreateLink(end_pin, start_pin);
                    }
                    else
                    {
                        CreateLink(start_pin, end_pin);
                    }
                }
            }
        }

        input_pin_id = 0;
        if (ax::NodeEditor::QueryNewNode(&input_pin_id))
        {
            Pin* input_pin = FindPin(input_pin_id);
            if (input_pin == nullptr || input_pin->link != nullptr)
            {
                ax::NodeEditor::RejectNewItem(ImColor(255, 0, 0), 2.0f);
            }
            else if (ax::NodeEditor::AcceptNewItem())
            {
                new_node_pin = input_pin;
                ax::NodeEditor::Suspend();
                ImGui::OpenPopup(add_node_popup_id.data());
                ax::NodeEditor::Resume();
            }
        }
    }
    ax::NodeEditor::EndCreate();
}

void App::DeleteNodesLinks()
{
    if (ax::NodeEditor::BeginDelete())
    {
        ax::NodeEditor::NodeId node_id = 0;
        while (ax::NodeEditor::QueryDeletedNode(&node_id))
        {
            if (ax::NodeEditor::AcceptDeletedItem())
            {
                DeleteNode(node_id);
            }
        }

        ax::NodeEditor::LinkId link_id = 0;
        while (ax::NodeEditor::QueryDeletedLink(&link_id))
        {
            if (ax::NodeEditor::AcceptDeletedItem())
            {
                DeleteLink(link_id);
            }
        }
    }
    ax::NodeEditor::EndDelete();
}

void App::AddNewNode()
{
    ax::NodeEditor::Suspend();
    if (ax::NodeEditor::ShowBackgroundContextMenu())
    {
        new_node_pin = nullptr;
        ImGui::OpenPopup(add_node_popup_id.data());
    }
    ax::NodeEditor::Resume();

    // We can't use IsWindowAppearing to detect the first frame as
    // we need new_node_position before BeginPopup for the
    // window max size computation. popup_opened is thus needed
    if (ImGui::IsPopupOpen(add_node_popup_id.data()) && !popup_opened)
    {
        popup_opened = true;
        new_node_position = ImGui::GetMousePos();
    }

    // Callback called to clean/reset state once popup is closed
    auto on_popup_close = [&]() {
        recipe_filter = "";
        popup_opened = false;
        new_node_pin = nullptr;
    };

    ax::NodeEditor::Suspend();
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(0.0f, 0.0f),
        ImVec2(
            ImGui::GetTextLineHeightWithSpacing() * 25.0f,
            // Max height is whatever space left to the bottom of the screen, clamped between 10 and 25 lines
            std::clamp(ImGui::GetMainViewport()->Size.y - ax::NodeEditor::CanvasToScreen(new_node_position).y, ImGui::GetTextLineHeightWithSpacing() * 10.0f, ImGui::GetTextLineHeightWithSpacing() * 25.0f)
        )
    );
    if (ImGui::BeginPopup(add_node_popup_id.data()))
    {
        int recipe_index = -1;
        if (ImGui::MenuItem("Merger"))
        {
            recipe_index = 0;
        }
        if (ImGui::MenuItem("Splitter"))
        {
            recipe_index = 1;
        }
        ImGui::Separator();
        // Stores the recipe index and a "match score" to sort them in the display
        std::vector<std::pair<int, size_t>> recipe_indices;
        const std::vector<std::unique_ptr<Recipe>>& recipes = Data::Recipes();
        recipe_indices.reserve(recipes.size());
        // If this is already linked to another node
        // only display matching recipes
        if (new_node_pin != nullptr && new_node_pin->item != nullptr)
        {
            const std::string& item_name = new_node_pin->item->new_line_name;
            for (int i = 0; i < recipes.size(); ++i)
            {
                if (recipe_index != -1)
                {
                    break;
                }
                const std::vector<CountedItem>& matching_pins = new_node_pin->direction == ax::NodeEditor::PinKind::Input ? recipes[i]->outs : recipes[i]->ins;
                for (int j = 0; j < matching_pins.size(); ++j)
                {
                    if (matching_pins[j].item->new_line_name == item_name)
                    {
                        recipe_indices.push_back({ i, 0 });
                        break;
                    }
                }
            }
        }
        // Otherwise display all recipes, with a filter option
        else if (recipe_index == -1 || (new_node_pin != nullptr && new_node_pin->item == nullptr))
        {
            if (ImGui::IsWindowAppearing())
            {
                // This automatically "click" on the input filter on the first popup frame.
                // A consequence is that it is no longer possible to reopen the menu at
                // another location without closing it first.
                // It's not necessarily a bad thing but may be a bit weird for first users.
                // Could use something to check if it's the case and reopen the popup further away ?
                // ?? ImGui::IsMouseClicked(config.ContextMenuButtonIndex) && !ImGui::IsWindowHovered() ??
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::InputTextWithHint("##recipe_filter", "Filter...", &recipe_filter);

            // If no filter, display all recipes in alphabetical order
            if (recipe_filter.empty())
            {
                for (int i = 0; i < recipes.size(); ++i)
                {
                    recipe_indices.push_back({ i, 0 });
                }
            }
            // Else display first all recipes with matching name, then matching ingredients
            else
            {
                // A recipe goes on top if it matched the search string "before" another
                // If they both matched at the same place, the alternate goes after
                auto scored_recipe_sorting = [&](const std::pair<int, size_t>& a, const std::pair<int, size_t>& b) {
                    return a.second < b.second || (a.second == b.second && !recipes[a.first]->alternate && recipes[b.first]->alternate);
                };
                for (int i = 0; i < recipes.size(); ++i)
                {
                    if (const size_t pos = recipes[i]->FindInName(recipe_filter); pos != std::string::npos)
                    {
                        recipe_indices.push_back({ i, pos });
                    }
                }
                std::stable_sort(recipe_indices.begin(), recipe_indices.end(), scored_recipe_sorting);
                const size_t num_recipe_match = recipe_indices.size();

                for (int i = 0; i < recipes.size(); ++i)
                {
                    if (recipes[i]->FindInName(recipe_filter) == std::string::npos)
                    {
                        if (const size_t pos = recipes[i]->FindInIngredients(recipe_filter); pos != std::string::npos)
                        {
                            recipe_indices.push_back({ i, pos });
                        }
                    }
                }
                std::stable_sort(recipe_indices.begin() + num_recipe_match, recipe_indices.end(), scored_recipe_sorting);
            }
        }

        ImGui::BeginTable("##recipe_selector", 3,
            ImGuiTableFlags_NoSavedSettings |
            ImGuiTableFlags_NoBordersInBody |
            ImGuiTableFlags_SizingStretchProp);
        const int col_flags =
            ImGuiTableColumnFlags_WidthStretch |
            ImGuiTableColumnFlags_NoResize |
            ImGuiTableColumnFlags_NoReorder |
            ImGuiTableColumnFlags_NoHide |
            ImGuiTableColumnFlags_NoClip |
            ImGuiTableColumnFlags_NoSort |
            ImGuiTableColumnFlags_NoHeaderWidth;
        ImGui::TableSetupColumn("##recipe_checkbox", col_flags);
        ImGui::TableSetupColumn("##recipe_names", col_flags);
        ImGui::TableSetupColumn("##items", col_flags);

        for (const auto [i, score_ignored] : recipe_indices)
        {
            if (settings.hide_spoilers && recipes[i]->is_spoiler)
            {
                continue;
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
            if (recipes[i]->alternate && ImGui::Checkbox(("##checkbox" + recipes[i]->name).c_str(), &settings.unlocked_alts.at(recipes[i].get())))
            {
                SaveSettings();
            }
            ImGui::PopStyleVar();
            ImGui::TableSetColumnIndex(1);
            ImGui::BeginDisabled(recipes[i]->alternate && !settings.unlocked_alts.at(recipes[i].get()));
            if (ImGui::MenuItem(recipes[i]->display_name.c_str()))
            {
                recipe_index = i + 2;
                // Need to duplicate the EndDisabled because of the break
                ImGui::EndDisabled();
                break;
            }
            ImGui::EndDisabled();
            ImGui::TableSetColumnIndex(2);

            recipes[i]->Render(false);
        }
        ImGui::EndTable();

        if (recipe_index != -1)
        {
            if (recipe_index == 0)
            {
                nodes.emplace_back(std::make_unique<MergerNode>(GetNextId(), std::bind(&App::GetNextId, this)));
            }
            else if (recipe_index == 1)
            {
                nodes.emplace_back(std::make_unique<SplitterNode>(GetNextId(), std::bind(&App::GetNextId, this)));
            }
            else
            {
                nodes.emplace_back(std::make_unique<CraftNode>(GetNextId(), recipes[recipe_index - 2].get(), std::bind(&App::GetNextId, this)));
            }
            ax::NodeEditor::SetNodePosition(nodes.back()->id, new_node_position);
            if (new_node_pin != nullptr)
            {
                const std::vector<std::unique_ptr<Pin>>& pins = new_node_pin->direction == ax::NodeEditor::PinKind::Input ? nodes.back()->outs : nodes.back()->ins;
                int pin_index = -1;
                for (int i = 0; i < pins.size(); ++i)
                {
                    if (new_node_pin->item == nullptr || pins[i]->item == nullptr || pins[i]->item == new_node_pin->item)
                    {
                        pin_index = i;
                        break;
                    }
                }
                if (pin_index != -1)
                {
                    // If we are dragging from an unset merger/setter, pull value instead of pushing it
                    if (new_node_pin->item == nullptr)
                    {
                        CreateLink(pins[pin_index].get(), new_node_pin);
                    }
                    else
                    {
                        CreateLink(new_node_pin, pins[pin_index].get());
                    }
                }
                updating_pins.push({ new_node_pin, Constraint::Strong });
            }
            on_popup_close();
        }
        ImGui::EndPopup();
    }
    else
    {
        on_popup_close();
    }
    ax::NodeEditor::Resume();
}

void App::RenderTooltips()
{
    for (const auto& s : frame_tooltips)
    {
        ImGui::SetTooltip("%s", s.c_str());
    }
    frame_tooltips.clear();
}

void App::RenderControlsPopup()
{
    if (ImGui::BeginTable("##controls_table", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
    {
        static constexpr std::array controls = {
            std::make_pair("Right click",         "Add node"),
            std::make_pair("Right click + mouse", "Move view"),
            std::make_pair("Left click",          "Select node/link"),
            std::make_pair("Left click + mouse",  "Move node/link"),
            std::make_pair("Mouse wheel",         "Zoom/Unzoom"),
            std::make_pair("Del",                 "Delete selection"),
            std::make_pair("F",                   "Show selection/full graph"),
            std::make_pair("Alt",                 "Disable grid snapping"),
            std::make_pair("Arrows",              "Nudge selection"),
            std::make_pair("Ctrl + A",            "Select all nodes"),
        };
        for (const auto [k, s] : controls)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(k);
            ImGui::TableSetColumnIndex(1);
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetColumnWidth() - ImGui::CalcTextSize(s).x));
            ImGui::TextUnformatted(s);
        }

        ImGui::EndTable();
    }

    if (!ImGui::IsWindowFocused())
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void App::CustomKeyControl()
{
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureKeyboard &&
        ImGui::IsKeyPressed(ImGuiKey_A, false) &&
        io.KeyCtrl)
    {
        for (const auto& n : nodes)
        {
            ax::NodeEditor::SelectNode(n->id, true);
        }
    }


    // If any user input happened, reset last time interaction
    for (const auto& k : io.KeysData)
    {
        if (k.Down)
        {
            last_time_interacted = std::chrono::steady_clock::now();
            break;
        }
    }

    if (ImGui::IsAnyMouseDown() || io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)
    {
        last_time_interacted = std::chrono::steady_clock::now();
    }
}
