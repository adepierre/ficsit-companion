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
#include <queue>
#include <stdexcept>
#include <unordered_set>

// #define WITH_SPOILERS

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
    // Spoilers are disabled since we are not just after a major release anymore
#ifdef WITH_SPOILERS
    settings.hide_spoilers = !json.contains("hide_spoilers") || json["hide_spoilers"].get<bool>(); // default true
#else
    settings.hide_spoilers = false;
#endif
    settings.hide_somersloop = json.contains("hide_somersloop") && json["hide_somersloop"].get<bool>(); // default false
    settings.unlocked_alts = {};

    for (const auto& r : Data::Recipes())
    {
        if (r->alternate)
        {
            settings.unlocked_alts[r.get()] = json.contains("unlocked_alts") && json["unlocked_alts"].contains(r->name.substr(1)) && json["unlocked_alts"][r->name.substr(1)].get<bool>();
        }
    }

    settings.hide_build_progress = !json.contains("hide_build_progress") || json["hide_build_progress"].get<bool>(); // default true

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

    Json::Object unlocked;
    for (const auto& [r, b] : settings.unlocked_alts)
    {
        // Remove the leading "*" from the alt recipe name
        unlocked[r->name.substr(1)] = b;
    }
    serialized["unlocked_alts"] = unlocked;

    serialized["hide_build_progress"] = settings.hide_build_progress;

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

    auto get_pin_index = [&](const Pin* p) -> int {
        const std::vector<std::unique_ptr<Pin>>& pins = p->direction == ax::NodeEditor::PinKind::Output ? p->node->outs : p->node->ins;
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
                { "pin", get_pin_index(l->start) }
            }},
            { "end", {
                { "node", get_node_index(l->end->node) },
                { "pin", get_pin_index(l->end) }
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

    // Load nodes
    std::vector<int> node_indices;
    node_indices.reserve(content["nodes"].size());
    size_t num_nodes = 0;
    for (const auto& n : content["nodes"].get_array())
    {
        try
        {
            nodes.emplace_back(Node::Deserialize(GetNextId(), std::bind(&App::GetNextId, this), n));
            ax::NodeEditor::SetNodePosition(nodes.back()->id, nodes.back()->pos);
            node_indices.push_back(num_nodes);
            num_nodes += 1;
        }
        catch (std::exception)
        {
            node_indices.push_back(-1);
        }
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
        // Make sure start is always an output and end an input
        start->direction == ax::NodeEditor::PinKind::Output ? start : end,
        end->direction == ax::NodeEditor::PinKind::Input ? end : start));
    start->link = links.back().get();
    end->link = links.back().get();
    if (start->node->IsOrganizer())
    {
        if (OrganizerNode* organizer_node = static_cast<OrganizerNode*>(start->node); organizer_node->item == nullptr)
        {
            organizer_node->ChangeItem(end->item);
        }
    }
    if (end->node->IsOrganizer())
    {
        if (OrganizerNode* organizer_node = static_cast<OrganizerNode*>(end->node); organizer_node->item == nullptr)
        {
            organizer_node->ChangeItem(start->item);
        }
    }
    Pin* real_end = end->direction == ax::NodeEditor::PinKind::Input ? end : start;
    if (real_end->node->IsSink())
    {
        real_end->item = (start->direction == ax::NodeEditor::PinKind::Output ? start : end)->item;
    }
    if (start->current_rate != end->current_rate)
    {
        updated_pins_new_rates.push_back({ start, start->current_rate });
    }
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
            if (start->node->IsOrganizer())
            {
                static_cast<OrganizerNode*>(start->node)->RemoveItemIfNotForced();
            }
        }
        if (Pin* end = (*it)->end; end != nullptr)
        {
            end->link = nullptr;
            // If either end was an organizer node, check the name is still valid
            if (end->node->IsOrganizer())
            {
                static_cast<OrganizerNode*>(end->node)->RemoveItemIfNotForced();
            }
            else if (end->node->IsSink())
            {
                end->item = nullptr;
                end->current_rate = 0;
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
    if (updated_pins_new_rates.size() == 0)
    {
        return;
    }

    // New rate for each pin involved in the current graph update
    std::unordered_map<const Pin*, FractionalNumber> all_pins_new_rates;

    auto new_rate_default_to_current_rate = [&](const Pin* p) {
        const auto it = all_pins_new_rates.find(p);
        if (it != all_pins_new_rates.end())
        {
            return it->second;
        }
        return p->current_rate;
    };

    bool balanced = false;
    while (!balanced)
    {
        // All pins updated while going from left to right
        std::unordered_set<const Pin*> updated_from_left;
        // All pins updated while going from right to left
        std::unordered_set<const Pin*> updated_from_right;
        // (having only one set instead of two messes things up when the graph
        // contains some loop like recycle plastic/rubber)

        // Current queue of pins that had their new rate set and need to propagate it
        std::queue<const Pin*> pins_to_propagate;

        // Propagate to find all pins involved in the current graph update
        // (ignoring their rate for now)
        size_t current_updated_pin_index = 0;
        while (true)
        {
            // If we don't have any pin to propagate, push the next
            // updated one (manually changed, or forced during a
            // previous iteration of the outter while loop)
            if (pins_to_propagate.empty())
            {
                if (current_updated_pin_index == updated_pins_new_rates.size())
                {
                    break;
                }
                const Pin* pin = updated_pins_new_rates[current_updated_pin_index].first;
                pins_to_propagate.push(pin);
                // We need to manually process the link here to prevent reupdating
                // this pin (but in the reverse left/right) from its linked one
                if (pin->link != nullptr)
                {
                    pins_to_propagate.push(pin->direction == ax::NodeEditor::PinKind::Input ? pin->link->start : pin->link->end);
                }
                current_updated_pin_index += 1;
            }

            const Pin* current_pin = pins_to_propagate.front();
            pins_to_propagate.pop();

            (current_pin->direction == ax::NodeEditor::PinKind::Input ? updated_from_left : updated_from_right).insert(current_pin);

            // Update parent node from this pin
            switch (current_pin->node->GetKind())
            {
            case Node::Kind::Craft:
            case Node::Kind::Group:
            case Node::Kind::GameSplitter:
            {
                std::unordered_set<const Pin*>& updated_for_inputs = current_pin->direction == ax::NodeEditor::PinKind::Input ? updated_from_right : updated_from_left;
                std::unordered_set<const Pin*>& updated_for_outputs = current_pin->direction == ax::NodeEditor::PinKind::Input ? updated_from_left : updated_from_right;
                for (const auto& p : current_pin->node->ins)
                {
                    // Update the other inputs from right/left (depending on the type of the current pin)
                    if (p.get() != current_pin && updated_for_inputs.find(p.get()) == updated_for_inputs.end())
                    {
                        updated_for_inputs.insert(p.get());
                        if (p->link != nullptr)
                        {
                            pins_to_propagate.push(p->link->start);
                        }
                    }
                }
                for (const auto& p : current_pin->node->outs)
                {
                    // Update outputs from left/right (depending on the type of the current pin)
                    if (p.get() != current_pin && updated_for_outputs.find(p.get()) == updated_for_outputs.end())
                    {
                        updated_for_outputs.insert(p.get());
                        if (p->link != nullptr)
                        {
                            pins_to_propagate.push(p->link->end);
                        }
                    }
                }
            }
                break;
            case Node::Kind::CustomSplitter:
            case Node::Kind::Merger:
                if (current_pin->direction == ax::NodeEditor::PinKind::Input)
                {
                    for (const auto& p : current_pin->node->outs)
                    {
                        // Update all outputs from left
                        if (updated_from_left.find(p.get()) == updated_from_left.end())
                        {
                            updated_from_left.insert(p.get());
                            if (p->link != nullptr)
                            {
                                pins_to_propagate.push(p->link->end);
                            }
                        }
                    }
                }
                else
                {
                    for (const auto& p : current_pin->node->ins)
                    {
                        // Update all inputs from right
                        if (updated_from_right.find(p.get()) == updated_from_right.end())
                        {
                            updated_from_right.insert(p.get());
                            if (p->link != nullptr)
                            {
                                pins_to_propagate.push(p->link->start);
                            }
                        }
                    }
                }
                break;
            case Node::Kind::Sink:
                // Nothing to propagate
                break;
            }
        }


        // Merge all pins that are potentially involved in the current graph update
        std::unordered_set<const Pin*> relevant_pins = updated_from_left;
        relevant_pins.insert(updated_from_right.begin(), updated_from_right.end());

        // Reset all previous flow
        for (auto& l : links)
        {
            l->flow = std::nullopt;
        }

        all_pins_new_rates.clear();
        current_updated_pin_index = 0;
        while (true)
        {
            // If we don't have any pin to propagate, push the next
            // updated one (manually changed, or forced during a
            // previous iteration of the outter while loop)
            if (pins_to_propagate.empty())
            {
                if (current_updated_pin_index == updated_pins_new_rates.size())
                {
                    break;
                }
                const auto& [p, r] = updated_pins_new_rates[current_updated_pin_index];
                pins_to_propagate.push(p);
                all_pins_new_rates[p] = r;
                current_updated_pin_index += 1;
            }

            const Pin* updated_pin = pins_to_propagate.front();
            pins_to_propagate.pop();

            switch (const Node::Kind kind = updated_pin->node->GetKind())
            {
            case Node::Kind::Craft:
            case Node::Kind::Group:
            {
                FractionalNumber node_new_rate;
                if (kind == Node::Kind::Craft)
                {
                    CraftNode* node = static_cast<CraftNode*>(updated_pin->node);
                    node_new_rate = all_pins_new_rates.at(updated_pin) / (
                        updated_pin->direction == ax::NodeEditor::PinKind::Output ?
                            updated_pin->base_rate * (1 + node->num_somersloop * node->recipe->building->somersloop_mult) :
                            updated_pin->base_rate
                    );
                }
                else // Group
                {
                    GroupNode* node = static_cast<GroupNode*>(updated_pin->node);
                    node_new_rate = all_pins_new_rates.at(updated_pin) / updated_pin->base_rate;
                }

                // Update new rates of all inputs pins (and linked pins if exists). Propagate from the linked pins
                for (const auto& p : updated_pin->node->ins)
                {
                    all_pins_new_rates[p.get()] = p->base_rate * node_new_rate;
                    if (p->link != nullptr)
                    {
                        Pin* linked_pin = p->link->start;
                        if (!linked_pin->link->flow.has_value() && linked_pin->current_rate != p->base_rate * node_new_rate)
                        {
                            linked_pin->link->flow = ax::NodeEditor::FlowDirection::Backward;
                        }
                        if (all_pins_new_rates.find(linked_pin) == all_pins_new_rates.end())
                        {
                            all_pins_new_rates[linked_pin] = p->base_rate * node_new_rate;
                            pins_to_propagate.push(linked_pin);
                        }
                    }
                }
                // Update new rates of all outputs pins (and linked pins if exists). Propagate from the linked pins
                for (const auto& p : updated_pin->node->outs)
                {
                    FractionalNumber new_pin_rate;
                    if (kind == Node::Kind::Craft)
                    {
                        CraftNode* node = static_cast<CraftNode*>(updated_pin->node);
                        new_pin_rate = p->base_rate * node_new_rate * (1 + (node->num_somersloop * node->recipe->building->somersloop_mult));
                    }
                    else // Group
                    {
                        new_pin_rate = p->base_rate * node_new_rate;
                    }
                    all_pins_new_rates[p.get()] = new_pin_rate;
                    if (p->link != nullptr)
                    {
                        Pin* linked_pin = p->link->end;
                        if (!linked_pin->link->flow.has_value() && linked_pin->current_rate != new_pin_rate)
                        {
                            linked_pin->link->flow = ax::NodeEditor::FlowDirection::Forward;
                        }
                        if (all_pins_new_rates.find(linked_pin) == all_pins_new_rates.end())
                        {
                            all_pins_new_rates[linked_pin] = new_pin_rate;
                            pins_to_propagate.push(linked_pin);
                        }
                    }
                }
            }
                break;
            case Node::Kind::CustomSplitter:
            case Node::Kind::Merger:
            {
                const std::vector<std::unique_ptr<Pin>>& single_pin = kind == Node::Kind::CustomSplitter ? updated_pin->node->ins : updated_pin->node->outs;
                const std::vector<std::unique_ptr<Pin>>& multi_pin = kind == Node::Kind::CustomSplitter ? updated_pin->node->outs : updated_pin->node->ins;

                // If single pin side is updated, update all multi pin side pins that have not already been updated
                if ((kind == Node::Kind::CustomSplitter && updated_pin->direction == ax::NodeEditor::PinKind::Input) ||
                    (kind == Node::Kind::Merger   && updated_pin->direction == ax::NodeEditor::PinKind::Output))
                {
                    FractionalNumber old_sum_to_update;
                    FractionalNumber sum_ignored_or_already_updated;
                    std::vector<const Pin*> pins_to_update;
                    // Sum all multipin side, and separate relevant from non-relevant ones
                    for (const auto& p : multi_pin)
                    {
                        const auto it = relevant_pins.find(p.get());
                        // Pins not updated by this graph operation
                        if (it == relevant_pins.end())
                        {
                            sum_ignored_or_already_updated += p->current_rate;
                        }
                        else
                        {
                            const auto it2 = all_pins_new_rates.find(*it);
                            // Already updated
                            if (it2 != all_pins_new_rates.end())
                            {
                                sum_ignored_or_already_updated += it2->second;
                            }
                            else
                            {
                                pins_to_update.push_back(p.get());
                                old_sum_to_update += p->current_rate;
                            }
                        }
                    }

                    // Update all multi pin side pins keeping the same ratio for each one
                    if (pins_to_update.size() != 0)
                    {
                        const FractionalNumber new_rate_single_pin = all_pins_new_rates.at(updated_pin);
                        // Node is already unbalanced, set all pins to update to 0
                        if (new_rate_single_pin < sum_ignored_or_already_updated)
                        {
                            for (const auto p : pins_to_update)
                            {
                                all_pins_new_rates[p] = 0;
                                if (p->link != nullptr)
                                {
                                    Pin* linked_pin = p->direction == ax::NodeEditor::PinKind::Output ? p->link->end : p->link->start;
                                    if (!linked_pin->link->flow.has_value() && linked_pin->current_rate != 0)
                                    {
                                        linked_pin->link->flow = linked_pin->direction == ax::NodeEditor::PinKind::Output ? ax::NodeEditor::FlowDirection::Backward : ax::NodeEditor::FlowDirection::Forward;
                                    }
                                    if (all_pins_new_rates.find(linked_pin) == all_pins_new_rates.end())
                                    {
                                        all_pins_new_rates[linked_pin] = 0;
                                        pins_to_propagate.push(linked_pin);
                                    }
                                }
                            }
                        }
                        // Otherwise, we need to split the remaining amount between all pins that needs updating, while keeping ratio
                        else
                        {
                            const FractionalNumber new_sum_updated = new_rate_single_pin - sum_ignored_or_already_updated;
                            for (const auto p : pins_to_update)
                            {
                                const FractionalNumber new_rate = old_sum_to_update.GetNumerator() == 0 ?
                                    new_sum_updated / pins_to_update.size() : // If the old sum was 0, just split evenly
                                    new_sum_updated * p->current_rate / old_sum_to_update;
                                all_pins_new_rates[p] = new_rate;
                                if (p->link != nullptr)
                                {
                                    Pin* linked_pin = p->direction == ax::NodeEditor::PinKind::Output ? p->link->end : p->link->start;
                                    if (!linked_pin->link->flow.has_value() && linked_pin->current_rate != new_rate)
                                    {
                                        linked_pin->link->flow = linked_pin->direction == ax::NodeEditor::PinKind::Output ? ax::NodeEditor::FlowDirection::Backward : ax::NodeEditor::FlowDirection::Forward;
                                    }
                                    if (all_pins_new_rates.find(linked_pin) == all_pins_new_rates.end())
                                    {
                                        all_pins_new_rates[linked_pin] = new_rate;
                                        pins_to_propagate.push(linked_pin);
                                    }
                                }
                            }
                        }
                    }
                }
                else // One of the multi-pin side is updated
                {
                    // Check if all the relevant pins for this graph update in the multi pin side have been updated
                    bool all_updated = true;
                    FractionalNumber sum_multi_pin;
                    for (const auto& p : multi_pin)
                    {
                        const bool is_relevant = relevant_pins.find(p.get()) != relevant_pins.end();
                        if (is_relevant)
                        {
                            const auto it = all_pins_new_rates.find(p.get());
                            all_updated = it != all_pins_new_rates.end();
                            sum_multi_pin += all_updated ? it->second : 0;
                        }
                        else
                        {
                            sum_multi_pin += p->current_rate;
                        }
                        if (!all_updated)
                        {
                            break;
                        }
                    }

                    // If so, we can now update the single pin side with the sum
                    if (all_updated)
                    {
                        all_pins_new_rates[single_pin[0].get()] = sum_multi_pin;
                        if (single_pin[0]->link != nullptr)
                        {
                            Pin* linked_pin = single_pin[0]->direction == ax::NodeEditor::PinKind::Output ? single_pin[0]->link->end : single_pin[0]->link->start;
                            if (!linked_pin->link->flow.has_value() && linked_pin->current_rate != sum_multi_pin)
                            {
                                linked_pin->link->flow = linked_pin->direction == ax::NodeEditor::PinKind::Output ? ax::NodeEditor::FlowDirection::Backward : ax::NodeEditor::FlowDirection::Forward;
                            }
                            if (all_pins_new_rates.find(linked_pin) == all_pins_new_rates.end())
                            {
                                all_pins_new_rates[linked_pin] = sum_multi_pin;
                                pins_to_propagate.push(linked_pin);
                            }
                        }
                    }
                    // Otherwise we wait for the others and do nothing here
                }

                // Also update linked pin if there is one
                if (updated_pin->link != nullptr)
                {
                    Pin* linked_pin = updated_pin->direction == ax::NodeEditor::PinKind::Output ? updated_pin->link->end : updated_pin->link->start;
                    if (!linked_pin->link->flow.has_value() && linked_pin->current_rate != all_pins_new_rates[updated_pin])
                    {
                        linked_pin->link->flow = linked_pin->direction == ax::NodeEditor::PinKind::Output ? ax::NodeEditor::FlowDirection::Backward : ax::NodeEditor::FlowDirection::Forward;
                    }
                    if (all_pins_new_rates.find(linked_pin) == all_pins_new_rates.end())
                    {
                        all_pins_new_rates[linked_pin] = all_pins_new_rates[updated_pin];
                        pins_to_propagate.push(linked_pin);
                    }
                }
            }
                break;
            case Node::Kind::GameSplitter:
            {
                // If single pin side is updated, update all multi pin side
                if (updated_pin->direction == ax::NodeEditor::PinKind::Input)
                {
                    const FractionalNumber single_pin_rate = all_pins_new_rates.at(updated_pin);
                    const FractionalNumber out_pin_rate = single_pin_rate / updated_pin->node->outs.size();
                    for (const auto& p : updated_pin->node->outs)
                    {
                        all_pins_new_rates[p.get()] = out_pin_rate;
                        if (p->link != nullptr)
                        {
                            Pin* linked_pin = p->link->end;
                            if (!linked_pin->link->flow.has_value() && linked_pin->current_rate != out_pin_rate)
                            {
                                linked_pin->link->flow = ax::NodeEditor::FlowDirection::Backward;
                            }
                            if (all_pins_new_rates.find(linked_pin) == all_pins_new_rates.end())
                            {
                                all_pins_new_rates[linked_pin] = out_pin_rate;
                                pins_to_propagate.push(linked_pin);
                            }
                        }
                    }
                    if (updated_pin->link != nullptr)
                    {
                        Pin* linked_pin = updated_pin->link->start;
                        if (!linked_pin->link->flow.has_value() && linked_pin->current_rate != single_pin_rate)
                        {
                            linked_pin->link->flow = ax::NodeEditor::FlowDirection::Forward;
                        }
                        if (all_pins_new_rates.find(linked_pin) == all_pins_new_rates.end())
                        {
                            all_pins_new_rates[linked_pin] = single_pin_rate;
                            pins_to_propagate.push(linked_pin);
                        }
                    }
                }
                else // One of the multi-pin side is updated
                {
                    // Update all other output pins
                    const FractionalNumber new_rate_out_pins = all_pins_new_rates.at(updated_pin);
                    for (const auto& p : updated_pin->node->outs)
                    {
                        all_pins_new_rates[p.get()] = new_rate_out_pins;
                        if (p->link != nullptr)
                        {
                            Pin* linked_pin = p->link->end;
                            if (!linked_pin->link->flow.has_value() && linked_pin->current_rate != new_rate_out_pins)
                            {
                                linked_pin->link->flow = ax::NodeEditor::FlowDirection::Forward;
                            }
                            if (all_pins_new_rates.find(linked_pin) == all_pins_new_rates.end())
                            {
                                all_pins_new_rates[linked_pin] = new_rate_out_pins;
                                pins_to_propagate.push(linked_pin);
                            }
                        }
                    }
                    // Update input pin
                    all_pins_new_rates[updated_pin->node->ins[0].get()] = new_rate_out_pins * updated_pin->node->outs.size();
                    if (updated_pin->node->ins[0]->link != nullptr)
                    {
                        Pin* linked_pin = updated_pin->node->ins[0]->link->start;
                        if (!linked_pin->link->flow.has_value() && linked_pin->current_rate != new_rate_out_pins * updated_pin->node->outs.size())
                        {
                            linked_pin->link->flow = ax::NodeEditor::FlowDirection::Backward;
                        }
                        if (all_pins_new_rates.find(linked_pin) == all_pins_new_rates.end())
                        {
                            all_pins_new_rates[linked_pin] = new_rate_out_pins * updated_pin->node->outs.size();
                            pins_to_propagate.push(linked_pin);
                        }
                    }
                }
            }
                break;
            case Node::Kind::Sink:
                // Nothing to update on the other inputs, just update the linked pin if there's one
                if (updated_pin->link != nullptr)
                {
                    Pin* linked_pin = updated_pin->link->start;
                    if (!linked_pin->link->flow.has_value() && linked_pin->current_rate != all_pins_new_rates.at(updated_pin))
                    {
                        linked_pin->link->flow = ax::NodeEditor::FlowDirection::Backward;
                    }
                    if (all_pins_new_rates.find(linked_pin) == all_pins_new_rates.end())
                    {
                        all_pins_new_rates[linked_pin] = all_pins_new_rates.at(updated_pin);
                        pins_to_propagate.push(linked_pin);
                    }
                }
                break;
            }
        }

        // Loop through all nodes looking for unbalanced splitter/merger affected by this change
        // this can happen due to cycle loops (e.g. recycled plastic/rubber). For each such node,
        // we need to also propagate up/downstream the other "non-updated" pins (which become
        // updated ones due to this adjustment). This will cause "more" update than strictly
        // required, but without that we'd have to detect each potential cycle and solve some
        // kind of equations based on in/out and I don't know how to do that.
        balanced = true;
        for (const auto& n : nodes)
        {
            if (!n->IsCustomSplitter() && !n->IsMerger())
            {
                continue;
            }

            const std::vector<std::unique_ptr<Pin>>& single_pin = n->GetKind() == Node::Kind::CustomSplitter ? n->ins : n->outs;
            const std::vector<std::unique_ptr<Pin>>& multi_pin = n->GetKind() == Node::Kind::CustomSplitter ? n->outs : n->ins;
            const auto it_single_pin = relevant_pins.find(single_pin[0].get());
            // If the single pin of this node is not relevant, we can skip it entirely
            if (it_single_pin == relevant_pins.end())
            {
                continue;
            }

            FractionalNumber sum_relevant;
            FractionalNumber sum_non_relevant;
            std::vector<const Pin*> non_relevant_pins;
            // Sum all multipin side, and separate relevant from non-relevant ones
            for (const auto& p : multi_pin)
            {
                const auto it = all_pins_new_rates.find(p.get());
                if (it == all_pins_new_rates.end())
                {
                    non_relevant_pins.push_back(p.get());
                    sum_non_relevant += p->current_rate;
                }
                else
                {
                    sum_relevant += it->second;
                }
            }

            // If this node is already fully updated
            if (non_relevant_pins.size() == 0)
            {
                continue;
            }

            // Unbalanced node or link, add the non-updated pin(s) to the list of pins to update,
            // with a new rate to keep the same ratio they had in the current merge/split
            if (sum_relevant + sum_non_relevant != new_rate_default_to_current_rate(single_pin[0].get()) ||
                (single_pin[0]->link != nullptr && new_rate_default_to_current_rate(single_pin[0]->link->start) != new_rate_default_to_current_rate(single_pin[0]->link->end))
            )
            {
                const FractionalNumber sum_relevant_old = single_pin[0]->current_rate - sum_non_relevant;
                FractionalNumber new_sum_non_relevant;
                if (sum_relevant_old.GetNumerator() == 0)
                {
                    if (sum_non_relevant.GetNumerator() == 0)
                    {
                        new_sum_non_relevant = sum_relevant;
                    }
                    // We can't keep the same ratio if the updated pins went from 0 to non-zero
                    // Try to add a new constraint on the single pin instead if it doesn't already have one
                    else if (std::find_if(
                        updated_pins_new_rates.begin(),
                        updated_pins_new_rates.end(),
                        [&](const std::pair<const Pin*, FractionalNumber>& p) {
                            return p.first == single_pin[0].get();
                        }) == updated_pins_new_rates.end())
                    {
                        updated_pins_new_rates.push_back({ single_pin[0].get(), sum_relevant + sum_non_relevant });
                        balanced = false;
                        break;
                    }
                    // Can't really do anything, just leave the node unbalanced and hope the user will see it
                    else
                    {
                        continue;
                    }
                }
                else
                {
                    new_sum_non_relevant = sum_non_relevant * sum_relevant / sum_relevant_old;
                }

                for (auto p : non_relevant_pins)
                {
                    updated_pins_new_rates.push_back({
                        p,
                        // If the old sum was 0, just split 1/N for each pin, else keep the same ratio
                        sum_non_relevant.GetNumerator() == 0 ? (new_sum_non_relevant / non_relevant_pins.size()) : (new_sum_non_relevant * p->current_rate / sum_non_relevant)
                    });
                }

                balanced = false;
                break;
            }
        }
    }

    // Set the updated new rates for each pin (and node)
    for (const auto& n : nodes)
    {
        for (const auto& p : n->ins)
        {
            const auto it = all_pins_new_rates.find(p.get());
            if (it != all_pins_new_rates.end())
            {
                p->current_rate = it->second;
                if (n->IsPowered())
                {
                    const FractionalNumber new_node_rate = it->second / p->base_rate;
                    PoweredNode* powered_node = static_cast<PoweredNode*>(n.get());
                    if (new_node_rate != powered_node->current_rate)
                    {
                        powered_node->UpdateRate(new_node_rate);
                    }
                }
            }
        }
        for (const auto& p : n->outs)
        {
            const auto it = all_pins_new_rates.find(p.get());
            if (it != all_pins_new_rates.end())
            {
                p->current_rate = it->second;
            }
        }
    }

    updated_pins_new_rates.clear();

    // Make sure all powered nodes are still valid regarding their recipe
    // (should always be true but just in case)
    for (auto& n : nodes)
    {
        if (!n->IsPowered())
        {
            continue;
        }

        PoweredNode* node = static_cast<PoweredNode*>(n.get());
        node->UpdateRate(node->current_rate);
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

void App::GroupSelectedNodes()
{
    std::vector<std::unique_ptr<Node>> selected_nodes;
    std::vector<std::unique_ptr<Link>> kept_links;

    auto process_link = [&](const Link* link) {
        if (link == nullptr)
        {
            return;
        }
        for (auto it = links.begin(); it != links.end(); ++it)
        {
            if (it->get() == link)
            {
                // If this link is between two group nodes, move it in the group
                if (ax::NodeEditor::IsNodeSelected((*it)->start->node->id) &&
                    ax::NodeEditor::IsNodeSelected((*it)->end->node->id))
                {
                    ax::NodeEditor::DeleteLink((*it)->id);
                    kept_links.emplace_back(std::move(*it));
                    links.erase(it);
                }
                // Else just remove it
                // TODO: can we keep the connections between the group and external nodes?
                // it should be doable if it's a "simple" link, but what about multiple links
                // to different pins with the same items ?
                else
                {
                    DeleteLink((*it)->id);
                }
                return;
            }
        }
    };

    for (auto it = nodes.begin(); it != nodes.end();)
    {
        if (ax::NodeEditor::IsNodeSelected((*it)->id))
        {
            for (const auto& p : (*it)->ins)
            {
                process_link(p->link);
            }
            for (const auto& p : (*it)->outs)
            {
                process_link(p->link);
            }
            ax::NodeEditor::DeleteNode((*it)->id);
            selected_nodes.emplace_back(std::move(*it));
            it = nodes.erase(it);
        }
        else
        {
            ++it;
        }
    }

    if (selected_nodes.empty())
    {
        return;
    }

    // Get the top left corner of this group
    ImVec2 min_pos(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());

    for (const auto& n : selected_nodes)
    {
        min_pos.x = std::min(min_pos.x, n->pos.x);
        min_pos.y = std::min(min_pos.y, n->pos.y);
    }

    // Offset all nodes in the group to store relative positions
    for (auto& n : selected_nodes)
    {
        n->pos = ImVec2(
            n->pos.x - min_pos.x,
            n->pos.y - min_pos.y
        );
    }

    nodes.emplace_back(std::make_unique<GroupNode>(GetNextId(), std::bind(&App::GetNextId, this), std::move(selected_nodes), std::move(kept_links)));
    nodes.back()->pos = min_pos;
    ax::NodeEditor::SetNodePosition(nodes.back()->id, min_pos);
    ax::NodeEditor::SelectNode(nodes.back()->id, false);
}

void App::UngroupSelectedNode()
{
    // Get the selected node
    GroupNode* group_node = nullptr;
    for (const auto& n : nodes)
    {
        if (ax::NodeEditor::IsNodeSelected(n->id))
        {
            // Should not happen because we check before calling the function but just in case...
            if (!n->IsGroup())
            {
                continue;
            }
            group_node = static_cast<GroupNode*>(n.get());
        }
    }
    // Should not happen because we check before calling the function but just in case...
    if (group_node == nullptr)
    {
        return;
    }

    const size_t num_node_before_add = nodes.size();
    const Json::Value serialized = group_node->Serialize();
    // Recreate the nodes of the group in the main graph
    for (auto& n : serialized["nodes"].get_array())
    {
        // Deserialize should always work as it's serialized in this version of the app (not loaded from a file)
        nodes.emplace_back(Node::Deserialize(GetNextId(), std::bind(&App::GetNextId, this), n));

        // Offset the new node with the group node position
        nodes.back()->pos.x += group_node->pos.x;
        nodes.back()->pos.y += group_node->pos.y;
        ax::NodeEditor::SetNodePosition(nodes.back()->id, nodes.back()->pos);
        ax::NodeEditor::SelectNode(nodes.back()->id, true);
    }

    // Recreate the internal links
    for (const auto& l : serialized["links"].get_array())
    {
        CreateLink(
            nodes[num_node_before_add + l["start"]["node"].get<int>()]->outs[l["start"]["pin"].get<int>()].get(),
            nodes[num_node_before_add + l["end"]["node"].get<int>()]->ins[l["end"]["pin"].get<int>()].get()
        );
    }

    // Delete old group node
    DeleteNode(group_node->id);
}


/******************************************************\
*              Rendering related functions             *
\******************************************************/
void App::Render()
{
    ax::NodeEditor::SetCurrentEditor(context);
    ax::NodeEditor::PushStyleColor(ax::NodeEditor::StyleColor_Flow, ImColor(1.0f, 1.0f, 0.0f));
    ax::NodeEditor::PushStyleColor(ax::NodeEditor::StyleColor_FlowMarker, ImColor(1.0f, 1.0f, 0.0f));
    ax::NodeEditor::PushStyleVar(ax::NodeEditor::StyleVar_SelectedNodeBorderWidth, 5.0f);

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
    ax::NodeEditor::PopStyleVar();
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
    if (ImGui::Checkbox("Hide somersloop amplifier", &settings.hide_somersloop))
    {
        SaveSettings();
    }
#ifdef WITH_SPOILERS
    if (ImGui::Checkbox("Hide 1.0 new advanced recipes", &settings.hide_spoilers))
    {
        SaveSettings();
    }
#endif
    if (ImGui::Checkbox("Compute power with equal clocks", &settings.power_equal_clocks))
    {
        SaveSettings();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s",
            "If set, the power per node will be calculated assuming all machines are set at the same clock value\n"
            "Otherwise, it will be calculated with machines at 100% and one last machine underclocked");
    }
    if (ImGui::Checkbox("Hide build progress", &settings.hide_build_progress))
    {
        SaveSettings();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s", "If set, build checkmark on craft nodes and overall progress bars won't be displayed");
    }
    if (!settings.hide_build_progress)
    {
        ImGui::SameLine();
        if (ImGui::Button("Reset progress"))
        {
            for (auto& n : nodes)
            {
                if (n->IsCraft())
                {
                    static_cast<CraftNode*>(n.get())->built = false;
                }
                else if (n->IsGroup())
                {
                    static_cast<GroupNode*>(n.get())->SetBuiltState(false);
                }
            }
        }
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

    std::map<const Item*, FractionalNumber, ItemPtrCompare> inputs;
    std::map<const Item*, FractionalNumber, ItemPtrCompare> outputs;
    std::map<const Item*, FractionalNumber, ItemPtrCompare> intermediates;
    std::map<std::string, FractionalNumber> total_machines;
    FractionalNumber all_machines;
    std::map<std::string, FractionalNumber> built_machines;
    FractionalNumber all_built_machines;
    std::map<std::string, std::map<const Recipe*, FractionalNumber, RecipePtrCompare>> detailed_machines;
    FractionalNumber total_sink_points;
    std::map<const Item*, FractionalNumber> detailed_sink_points;
    FractionalNumber total_power;
    std::map<const Recipe*, FractionalNumber> detailed_power;
    bool has_variable_power = false;

    // Gather all craft node stats (ins/outs/machines/power)
    for (const auto& n : nodes)
    {
        if (n->IsCraft())
        {
            for (const auto& p : n->ins)
            {
                inputs[p->item] += p->current_rate;
            }
            for (const auto& p : n->outs)
            {
                outputs[p->item] += p->current_rate;
            }

            const CraftNode* node = static_cast<const CraftNode*>(n.get());
            total_machines[node->recipe->building->name] += node->current_rate;
            all_machines += node->current_rate;
            detailed_machines[node->recipe->building->name][node->recipe] += node->current_rate;
            built_machines[node->recipe->building->name] += node->built ? node->current_rate : 0;
            all_built_machines += node->built ? node->current_rate : 0;
            total_power += settings.power_equal_clocks ? node->same_clock_power : node->last_underclock_power;
            detailed_power[node->recipe] += settings.power_equal_clocks ? node->same_clock_power : node->last_underclock_power;
            has_variable_power |= node->recipe->building->variable_power;
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

            total_power += settings.power_equal_clocks ? node->same_clock_power : node->last_underclock_power;
            has_variable_power |= node->variable_power;
            for (const auto& [k, v] : node->total_machines)
            {
                total_machines[k] += v;
                all_machines += v;
            }
            for (const auto& [k, v] : node->built_machines)
            {
                built_machines[k] += v;
                all_built_machines += v;
            }
            for (const auto& [k, v] : node->detailed_machines)
            {
                for (const auto& [k2, v2] : v)
                {
                    detailed_machines[k][k2] += v2;
                }
            }
            for (const auto& [k, v] : (settings.power_equal_clocks ? node->detailed_power_same_clock : node->detailed_power_last_underclock))
            {
                detailed_power[k] += v;
            }

            for (const auto& [k, v] : node->detailed_sinked_points)
            {
                total_sink_points += v;
                detailed_sink_points[k] += v;
            }
        }
        else if (n->IsSink())
        {
            for (const auto& p : n->ins)
            {
                if (p->item != nullptr)
                {
                    inputs[p->item] += p->current_rate;
                    total_sink_points += p->current_rate * p->item->sink_value;
                    detailed_sink_points[p->item] += p->current_rate * p->item->sink_value;
                }
            }
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

    if (!settings.hide_build_progress)
    {
        ImGui::SeparatorText("Build Progress");

        // Progress bar color
        ImGui::PushStyleColor(ImGuiCol_::ImGuiCol_PlotHistogram, ImVec4(0, 0.5, 0, 1));
        // No visible color change when hovered/click
        ImGui::PushStyleColor(ImGuiCol_::ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_::ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
        const bool display_build_details = ImGui::TreeNodeEx("##build_progress", ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth);
        ImGui::PopStyleColor();
        ImGui::PopStyleColor();

        // Displayed over the TreeNodeEx element (same line)
        ImGui::SameLine();
        ImGui::ProgressBar(all_machines.GetNumerator() == 0 ? 0.0f : static_cast<float>((all_built_machines / all_machines).GetValue()));
        float max_machine_name_width = 0.0f;
        for (auto& [machine, f] : built_machines)
        {
            max_machine_name_width = std::max(max_machine_name_width, ImGui::CalcTextSize(machine.c_str()).x);
        }
        // Detailed list of recipes if the tree node is open
        if (display_build_details)
        {
            ImGui::Indent();
            for (auto& [machine, f] : built_machines)
            {
                ImGui::TextUnformatted(machine.c_str());
                ImGui::SameLine();
                ImGui::Dummy(ImVec2(max_machine_name_width - ImGui::CalcTextSize(machine.c_str()).x, 0.0f));
                ImGui::SameLine();
                ImGui::ProgressBar((f / total_machines[machine]).GetValue());
            }

            ImGui::Unindent();
            ImGui::TreePop();
        }
        ImGui::PopStyleColor();
    }

    ImGui::SeparatorText(has_variable_power ? "Average Power Consumption" : "Power Consumption");
    if (total_power.GetNumerator() != 0)
    {
        const float power_width = ImGui::CalcTextSize("000000.00").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        std::vector<std::pair<const Recipe*, FractionalNumber>> sorted_detailed_power(detailed_power.begin(), detailed_power.end());
        std::stable_sort(sorted_detailed_power.begin(), sorted_detailed_power.end(), [](const auto& a, const auto& b) {
            return a.second.GetValue() > b.second.GetValue();
        });

        // No visible color change when hovered/click
        ImGui::PushStyleColor(ImGuiCol_::ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_::ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
        const bool display_power_details = ImGui::TreeNodeEx("##power", ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth);
        ImGui::PopStyleColor();
        ImGui::PopStyleColor();

        // Displayed over the TreeNodeEx element (same line)
        ImGui::SameLine();
        total_power.RenderInputText("##power", true, false, power_width);
        ImGui::SameLine();
        ImGui::Text("%sMW", has_variable_power ? "~" : "");
        // Detailed list of recipes if the tree node is open
        if (display_power_details)
        {
            ImGui::Indent();
            for (auto& [recipe, p] : sorted_detailed_power)
            {
                p.RenderInputText("##power", true, false, power_width);
                ImGui::SameLine();
                ImGui::Text("%sMW", recipe->building->variable_power ? "~" : "");
                ImGui::SameLine();
                recipe->Render();
            }

            ImGui::Unindent();
            ImGui::TreePop();
        }
    }

    ImGui::SeparatorText("Sink points");
    if (total_sink_points.GetNumerator() != 0)
    {
        const float sink_points_width = ImGui::CalcTextSize("00000000.00").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        std::vector<std::pair<const Item*, FractionalNumber>> sorted_sink_points(detailed_sink_points.begin(), detailed_sink_points.end());
        std::stable_sort(sorted_sink_points.begin(), sorted_sink_points.end(), [](const auto& a, const auto& b) {
            return a.second.GetValue() > b.second.GetValue();
            });

        // No visible color change when hovered/click
        ImGui::PushStyleColor(ImGuiCol_::ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_::ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
        const bool display_sink_points_details = ImGui::TreeNodeEx("##sink_points", ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth);
        ImGui::PopStyleColor();
        ImGui::PopStyleColor();

        // Displayed over the TreeNodeEx element (same line)
        ImGui::SameLine();
        total_sink_points.RenderInputText("##sink_points", true, true, sink_points_width);
        ImGui::SameLine();
        ImGui::TextUnformatted("Points");
        // Detailed list of items if the tree node is open
        if (display_sink_points_details)
        {
            ImGui::Indent();
            for (auto& [item, p] : sorted_sink_points)
            {
                p.RenderInputText("##sink_points", true, true, sink_points_width);
                ImGui::SameLine();
                ImGui::Image((void*)(intptr_t)item->icon_gl_index, ImVec2(ImGui::GetTextLineHeightWithSpacing(), ImGui::GetTextLineHeightWithSpacing()));
                ImGui::SameLine();
                ImGui::TextUnformatted(item->name.c_str());
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    ImGui::SetTooltip("%s", item->name.c_str());
                }
            }
            ImGui::Unindent();
            ImGui::TreePop();
        }
    }

    const float rate_width = ImGui::CalcTextSize("0000.000").x + ImGui::GetStyle().FramePadding.x * 2.0f;
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
        n.RenderInputText("##rate", true, true, rate_width);
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
                n2.RenderInputText("##rate", true, true, rate_width);
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
        if (const auto out_it = outputs.find(item); out_it != outputs.end())
        {
            // More output than input, don't display this in inputs
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
        n.RenderInputText("##rate", true, true, rate_width);
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
        n.RenderInputText("##rate", true, true, rate_width);
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
        n.RenderInputText("##rate", true, true, rate_width);
        ImGui::SameLine();
        ImGui::Image((void*)(intptr_t)item->icon_gl_index, ImVec2(ImGui::GetTextLineHeightWithSpacing(), ImGui::GetTextLineHeightWithSpacing()));
        ImGui::SameLine();
        ImGui::TextUnformatted(item->name.c_str());
    }
}

void App::RenderNodes()
{
    const float rate_width = ImGui::CalcTextSize("000.000").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const float somersloop_width = ImGui::CalcTextSize("4").x + ImGui::GetStyle().FramePadding.x * 2.0f;
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

            const bool p1_is_above = l1 != nullptr && (p1->direction == ax::NodeEditor::PinKind::Input ? l1->start : l1->end)->node->pos.y < p1->node->pos.y;
            const bool p2_is_above = l2 != nullptr && (p2->direction == ax::NodeEditor::PinKind::Input ? l2->start : l2->end)->node->pos.y < p1->node->pos.y;

            // Both are linked and above
            if (p1_is_above && p2_is_above)
            {
                return (p1->direction == ax::NodeEditor::PinKind::Input && l1->start->node->pos.y < l2->start->node->pos.y) ||
                    (p1->direction == ax::NodeEditor::PinKind::Output && l1->end->node->pos.y < l2->end->node->pos.y);
            }

            // p1 is above, p2 is either unlinked or below
            if (p1_is_above)
            {
                return true;
            }

            // p2 is above, p1 is either unlinked or below
            if (p2_is_above)
            {
                return false;
            }

            // No link, keep default order
            if (l1 == nullptr && l2 == nullptr)
            {
                return i1 < i2;
            }

            // p1 isn't linked, p2 is and we know it's not above, so p1 < p2
            if (l1 == nullptr)
            {
                return true;
            }

            // p2 isn't linked, p1 is and we know it's not above, so p2 < p1
            if (l2 == nullptr)
            {
                return false;
            }

            // Both are linked and below
            return (p1->direction == ax::NodeEditor::PinKind::Input && l1->start->node->pos.y < l2->start->node->pos.y) ||
                (p1->direction == ax::NodeEditor::PinKind::Output && l1->end->node->pos.y < l2->end->node->pos.y);
        });
    };

    for (const auto& node : nodes)
    {
        int node_pushed_style = 0;
        if (node->IsOrganizer() && !static_cast<OrganizerNode*>(node.get())->IsBalanced() ||
            node->IsGroup() && static_cast<GroupNode*>(node.get())->loading_error)
        {
            ax::NodeEditor::PushStyleColor(ax::NodeEditor::StyleColor_NodeBorder, ImColor(255, 0, 0));
            node_pushed_style += 1;
        }
        if (!settings.hide_build_progress && (
            (node->IsCraft() && static_cast<CraftNode*>(node.get())->built) ||
            (node->IsGroup() && static_cast<GroupNode*>(node.get())->built_machines == static_cast<GroupNode*>(node.get())->total_machines)
        ))
        {
            ax::NodeEditor::PushStyleColor(ax::NodeEditor::StyleColor_NodeBorder, ImColor(0, 255, 0));
            node_pushed_style += 1;
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
                {
                    CraftNode* craft_node = static_cast<CraftNode*>(node.get());
                    ImGui::Spring(1.0f);
                    ImGui::TextUnformatted(craft_node->recipe->display_name.c_str());
                    ImGui::Spring(1.0f);
                    if (!settings.hide_build_progress)
                    {
                        ImGui::PushStyleVar(ImGuiStyleVar_::ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                        ImGui::Checkbox("##craft_built", &craft_node->built);
                        ImGui::PopStyleVar();
                    }
                }
                    break;
                case Node::Kind::Merger:
                    ImGui::TextUnformatted("Merger");
                    break;
                case Node::Kind::CustomSplitter:
                    ImGui::TextUnformatted("Splitter*");
                    break;
                case Node::Kind::GameSplitter:
                    ImGui::TextUnformatted("Splitter");
                    break;
                case Node::Kind::Sink:
                    ImGui::TextUnformatted("Sink");
                    break;
                case Node::Kind::Group:
                {
                    GroupNode* group_node = static_cast<GroupNode*>(node.get());
                    ImGui::Spring(1.0f);
                    ImGui::TextUnformatted("Group");
                    ImGui::Spring(0.0f);
                    ImGui::SetNextItemWidth(std::max(ImGui::CalcTextSize(group_node->name.c_str()).x, ImGui::CalcTextSize("Name...").x) + ImGui::GetStyle().FramePadding.x * 4.0f);
                    ImGui::InputTextWithHint("##name", "Name...", &group_node->name);
                    ImGui::Spring(1.0f);
                    if (!settings.hide_build_progress)
                    {
                        ImGui::PushStyleVar(ImGuiStyleVar_::ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                        bool is_built = group_node->built_machines == group_node->total_machines;
                        if (ImGui::Checkbox("##group_built", &is_built))
                        {
                            group_node->SetBuiltState(is_built);
                        }
                        ImGui::PopStyleVar();
                    }
                    break;
                }
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

                    std::optional<int> removed_input_idx = std::nullopt;
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
                            if (node->IsMerger() || node->IsSink())
                            {
                                ImGui::BeginDisabled(node->ins.size() == 1);
                                if (ImGui::Button("x"))
                                {
                                    // We can't remove it directly as we are currently looping through the pins
                                    removed_input_idx = idx;
                                }
                                ImGui::EndDisabled();
                            }
                            ImGui::Spring(0.0f);
                            p->current_rate.RenderInputText("##rate", false, false, rate_width);
                            if (ImGui::IsItemDeactivatedAfterEdit())
                            {
                                try
                                {
                                    updated_pins_new_rates.push_back({ p.get(), p->current_rate.GetStringFloat() });
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

                            if (node->IsPowered() || (node->IsSink() && p->item != nullptr))
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
                    if (node->IsMerger() || node->IsSink())
                    {
                        ImGui::BeginHorizontal("add_input_+_buttton");
                        ImGui::Spring(1.0f, 0.0f);
                        if (ImGui::Button("+"))
                        {
                            node->ins.emplace_back(std::make_unique<Pin>(
                                GetNextId(),
                                ax::NodeEditor::PinKind::Input,
                                node.get(),
                                node->IsMerger() ? static_cast<MergerNode*>(node.get())->item : nullptr) // Merger or Sink
                            );
                        }
                        ImGui::Spring(1.0f, 0.0f);
                        ImGui::EndHorizontal();
                        if (removed_input_idx.has_value())
                        {
                            const int idx = removed_input_idx.value();
                            if (node->ins[sorted_pin_indices[idx]]->link != nullptr)
                            {
                                DeleteLink(node->ins[sorted_pin_indices[idx]]->link->id);
                            }
                            node->ins.erase(node->ins.begin() + sorted_pin_indices[idx]);
                            if (node->IsMerger()) // Sink doesn't have any output to update
                            {
                                FractionalNumber sum_inputs;
                                for (const auto& p : node->ins)
                                {
                                    sum_inputs += p->current_rate;
                                }
                                updated_pins_new_rates.push_back({ node->outs[0].get(), sum_inputs });
                            }
                        }
                    }
                    ImGui::Spring(1.0f, 0.0f);
                }
                ImGui::EndVertical();

                ImGui::Spring(1.0f);

                ImGui::BeginVertical("outputs", ImVec2(0, 0), 1.0f); // Align all elements on the right of the column
                {
                    // Set where the link will connect to (right center)
                    ax::NodeEditor::PushStyleVar(ax::NodeEditor::StyleVar_PivotAlignment, ImVec2(1.0f, 0.5f));
                    ax::NodeEditor::PushStyleVar(ax::NodeEditor::StyleVar_PivotSize, ImVec2(0, 0));
                    std::optional<int> removed_output_idx = std::nullopt;
                    sort_pin_indices(node->outs);
                    for (int idx = 0; idx < node->outs.size(); ++idx)
                    {
                        const auto& p = node->outs[sorted_pin_indices[idx]];
                        ax::NodeEditor::BeginPin(p->id, p->direction);
                        ImGui::BeginHorizontal(p->id.AsPointer());
                        {
                            if (node->IsPowered())
                            {
                                ImGui::Spring(0.0f);
                                ImGui::TextUnformatted(p->item->new_line_name.c_str());
                                ImGui::Spring(0.0f);
                                ImGui::Image((void*)(intptr_t)p->item->icon_gl_index, ImVec2(ImGui::GetTextLineHeightWithSpacing(), ImGui::GetTextLineHeightWithSpacing()));
                            }
                            ImGui::Spring(0.0f);
                            p->current_rate.RenderInputText("##rate", false, false, rate_width);
                            if (ImGui::IsItemDeactivatedAfterEdit())
                            {
                                try
                                {
                                    updated_pins_new_rates.push_back({ p.get(), p->current_rate.GetStringFloat() });
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
                            if (node->IsCustomSplitter() || node->IsGameSplitter())
                            {
                                ImGui::BeginDisabled(node->outs.size() == 1);
                                if (ImGui::Button("x"))
                                {
                                    // We can't remove it directly as we are currently looping through the pins
                                    removed_output_idx = idx;
                                }
                                ImGui::EndDisabled();
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
                    if (node->IsCustomSplitter() || node->IsGameSplitter())
                    {
                        OrganizerNode* org_node = static_cast<OrganizerNode*>(node.get());
                        ImGui::BeginHorizontal("add_output_+_buttton");
                        ImGui::Spring(1.0f, 0.0f);
                        if (ImGui::Button("+"))
                        {
                            org_node->outs.emplace_back(std::make_unique<Pin>(GetNextId(), ax::NodeEditor::PinKind::Output, org_node, org_node->item));
                            if (node->IsGameSplitter())
                            {
                                for (const auto& p : node->outs)
                                {
                                    updated_pins_new_rates.push_back({ p.get(), node->ins[0]->current_rate / node->outs.size() });
                                }
                            }
                        }
                        ImGui::Spring(1.0f, 0.0f);
                        ImGui::EndHorizontal();
                        if (removed_output_idx.has_value())
                        {
                            const int idx = removed_output_idx.value();
                            if (node->outs[sorted_pin_indices[idx]]->link != nullptr)
                            {
                                DeleteLink(node->outs[sorted_pin_indices[idx]]->link->id);
                            }
                            node->outs.erase(node->outs.begin() + sorted_pin_indices[idx]);
                            if (node->IsCustomSplitter())
                            {
                                FractionalNumber sum_outputs;
                                for (const auto& p : node->outs)
                                {
                                    sum_outputs += p->current_rate;
                                }
                                updated_pins_new_rates.push_back({ node->ins[0].get(), sum_outputs });
                            }
                            else // GameSplitter
                            {
                                for (const auto& p : node->outs)
                                {
                                    updated_pins_new_rates.push_back({ p.get(), node->ins[0]->current_rate / node->outs.size() });
                                }
                            }
                        }
                    }
                    ImGui::Spring(1.0f, 0.0f);
                }
                ImGui::EndVertical();
            }
            ImGui::EndHorizontal();

            ImGui::BeginHorizontal("bottom");
            {
                if (node->IsPowered())
                {
                    ImGui::Spring(0.0f);
                    PoweredNode* powered_node = static_cast<PoweredNode*>(node.get());
                    (settings.power_equal_clocks ? powered_node->same_clock_power : powered_node->last_underclock_power).RenderInputText("##power", true, false);
                    ImGui::Spring(0.0f);
                    ImGui::Text("%sMW", powered_node->HasVariablePower() ? "~" : "");
                    if (powered_node->HasVariablePower() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    {
                        frame_tooltips.push_back("Average power");
                    }
                    ImGui::Spring(1.0f);
                    powered_node->current_rate.RenderInputText("##rate", false, false, rate_width);
                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        // Try updating the rate with the new string value
                        try
                        {
                            powered_node->UpdateRate(FractionalNumber(powered_node->current_rate.GetStringFloat()));
                            for (auto& p : powered_node->ins)
                            {
                                updated_pins_new_rates.push_back({ p.get(), p->current_rate });
                            }
                            for (auto& p : powered_node->outs)
                            {
                                updated_pins_new_rates.push_back({ p.get(), p->current_rate });
                            }
                        }
                        // Rollback to previous value if the user input is not a valid fractional number
                        catch (const std::domain_error&)
                        {
                            powered_node->UpdateRate(FractionalNumber(powered_node->current_rate.GetNumerator(), powered_node->current_rate.GetDenominator()));
                        }
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    {
                        frame_tooltips.push_back(powered_node->current_rate.GetStringFraction());
                    }
                    if (node->IsGroup())
                    {
                        ImGui::Spring(1.0f);
                    }
                    else if (node->IsCraft())
                    {
                        CraftNode* craft_node = static_cast<CraftNode*>(node.get());
                        ImGui::Spring(0.0f);
                        ImGui::TextUnformatted(craft_node->recipe->building->name.c_str());
                        if (
                            // Override settings if it's not 0 (for example if the production chain is imported)
                            (settings.hide_somersloop && craft_node->num_somersloop.GetNumerator() == 0) ||
                            // Don't display somersloop if this building can't have one
                            craft_node->recipe->building->somersloop_mult.GetNumerator() == 0 ||
                            // Don't display somersloop for power generators
                            craft_node->recipe->building->power < 0.0
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
                                    craft_node->UpdateRate(craft_node->current_rate);
                                    for (auto& p : craft_node->outs)
                                    {
                                        updated_pins_new_rates.push_back({ p.get(), p->current_rate });
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
                }
                else if (node->IsOrganizer())
                {
                    ImGui::Spring(1.0f);
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
                else if (node->IsSink())
                {
                    ImGui::Spring(1.0f);
                    FractionalNumber sum_sink;
                    for (const auto& i : node->ins)
                    {
                        if (i->item != nullptr)
                        {
                            sum_sink += i->current_rate * i->item->sink_value;
                        }
                    }
                    sum_sink.RenderInputText("##points", true, false, 0.0f);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    {
                        frame_tooltips.push_back(sum_sink.GetStringFraction());
                    }
                    ImGui::Spring(0.0f);
                    ImGui::TextUnformatted("points");
                    ImGui::Spring(1.0f);
                }
            }
            ImGui::EndHorizontal();
        }
        ImGui::EndVertical();
        ImGui::PopID();
        ax::NodeEditor::EndNode();
        for (int i = 0; i < node_pushed_style; ++i)
        {
            ax::NodeEditor::PopStyleColor();
        }
    }
}

void App::RenderLinks()
{
    for (const auto& link : links)
    {
        ImColor link_color;
        if (link->start->current_rate != link->end->current_rate)
        {
            link_color = ImColor(1.0f, 0.0f, 0.0f); // Red
        }
        else if (link->end->node->IsSink() && (
            link->start->item == nullptr || link->end->item == nullptr ||
            link->start->item->sink_value == 0 || link->end->item->sink_value == 0)
        )
        {
            link_color = ImColor(1.0f, 0.5f, 0.0f); // Orange
        }
        else
        {
            link_color = ImColor(0.0f, 1.0f, 0.0f); // Green
        }

        ax::NodeEditor::Link(link->id, link->start_id, link->end_id, link_color);
        if (link->flow.has_value())
        {
            ax::NodeEditor::Flow(link->id, link->flow.value());
            link->flow = std::nullopt;
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
                    // If we are dragging from a default initialized 0 pin of an organizer node, pull value instead of pushing it
                    if ((start_pin->node->IsOrganizer() || start_pin->node->IsSink()) && start_pin->current_rate.GetNumerator() == 0)
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
    enum RecipeSelectionIndex : int
    {
        None = -1,
        Merger,
        CustomSplitter,
        GameSplitter,
        Sink,
        FIRST_REAL_RECIPE_INDEX,
    };

    if (ImGui::BeginPopup(add_node_popup_id.data()))
    {
        int recipe_index = RecipeSelectionIndex::None;
        if (ImGui::MenuItem("Merger"))
        {
            recipe_index = RecipeSelectionIndex::Merger;
        }
        if (ImGui::MenuItem("Splitter*"))
        {
            recipe_index = RecipeSelectionIndex::CustomSplitter;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", "Splitter with independant output rates");
        }
        if (ImGui::MenuItem("Splitter"))
        {
            recipe_index = RecipeSelectionIndex::GameSplitter;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", "Splitter with equals output rates");
        }
        if (ImGui::MenuItem("Sink"))
        {
            recipe_index = RecipeSelectionIndex::Sink;
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
                if (recipe_index != RecipeSelectionIndex::None)
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
        else if (recipe_index == RecipeSelectionIndex::None || (new_node_pin != nullptr && new_node_pin->item == nullptr))
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
                recipe_index = i + RecipeSelectionIndex::FIRST_REAL_RECIPE_INDEX;
                // Need to duplicate the EndDisabled because of the break
                ImGui::EndDisabled();
                break;
            }
            ImGui::EndDisabled();
            ImGui::TableSetColumnIndex(2);

            recipes[i]->Render(false);
        }
        ImGui::EndTable();

        if (recipe_index != RecipeSelectionIndex::None)
        {
            switch (recipe_index)
            {
            case RecipeSelectionIndex::None:
                break; // Should not happen because of the if above
            case RecipeSelectionIndex::Merger:
                nodes.emplace_back(std::make_unique<MergerNode>(GetNextId(), std::bind(&App::GetNextId, this)));
                break;
            case RecipeSelectionIndex::CustomSplitter:
                nodes.emplace_back(std::make_unique<CustomSplitterNode>(GetNextId(), std::bind(&App::GetNextId, this)));
                break;
            case RecipeSelectionIndex::GameSplitter:
                nodes.emplace_back(std::make_unique<GameSplitterNode>(GetNextId(), std::bind(&App::GetNextId, this)));
                break;
            case RecipeSelectionIndex::Sink:
                nodes.emplace_back(std::make_unique<SinkNode>(GetNextId(), std::bind(&App::GetNextId, this)));
                break;
            default:
                nodes.emplace_back(std::make_unique<CraftNode>(GetNextId(), recipes[recipe_index - RecipeSelectionIndex::FIRST_REAL_RECIPE_INDEX].get(), std::bind(&App::GetNextId, this)));
                break;
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
                    // If we are dragging from a default initialized 0 pin of an organizer node, pull value instead of pushing it
                    if ((new_node_pin->node->IsOrganizer() || new_node_pin->node->IsSink()) && new_node_pin->current_rate.GetNumerator() == 0)
                    {
                        CreateLink(pins[pin_index].get(), new_node_pin);
                    }
                    else
                    {
                        CreateLink(new_node_pin, pins[pin_index].get());
                    }
                }
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
            std::make_pair("Ctrl + G",            "Group/Ungroup nodes"),
            std::make_pair("Ctrl + Left click",   "Add to selection"),
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

    // Ctrl + A, select all
    if (!io.WantCaptureKeyboard &&
        ImGui::IsKeyPressed(ImGuiKey_A, false) &&
        io.KeyCtrl)
    {
        for (const auto& n : nodes)
        {
            ax::NodeEditor::SelectNode(n->id, true);
        }
    }

    // Ctrl + G, group/ungroup selected nodes
    if (!io.WantCaptureKeyboard &&
        ImGui::IsKeyPressed(ImGuiKey_G, false) &&
        io.KeyCtrl)
    {
        size_t num_selected = 0;
        bool group_selected = false;
        for (const auto& n : nodes)
        {
            if (ax::NodeEditor::IsNodeSelected(n->id))
            {
                num_selected += 1;
                // If we have multiple node selected, it's a group action
                if (num_selected > 1)
                {
                    break;
                }
                group_selected = n->IsGroup();
                // If we have at least one not group node, it's a group action
                if (!group_selected)
                {
                    break;
                }
            }
        }

        if (num_selected == 1 && group_selected)
        {
            UngroupSelectedNode();
        }
        else if (num_selected > 0)
        {
            GroupSelectedNodes();
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
