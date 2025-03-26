#pragma once

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <imgui_node_editor.h>

#include "fractional_number.hpp"

struct Link;
struct Node;
struct Pin;
struct Recipe;

class App
{
public:
    App();
    ~App();
    void Render();

public:
    /// @brief Save current session (should NOT require an active ImGui context)
    void SaveSession();

    bool HasRecentInteraction() const;

private:
    /// @brief Load saved session if present
    void LoadSession();

    void LoadSettings();
    void SaveSettings() const;

    /// @brief Serialize the app state to a string
    /// @return Serialized state of this app
    std::string Serialize() const;

    /// @brief Restore app state from a string
    /// @param s Serialized app state to load
    void Deserialize(const std::string& s);

    /// @brief Get the next available id for node-editor
    /// @return The next id to use
    unsigned long long int GetNextId();

    /// @brief Search for a Pin given its id
    /// @param id The pin id
    /// @return A pointer to the Pin, nullptr if not found
    Pin* FindPin(ax::NodeEditor::PinId id) const;

    /// @brief Create a link between two pins, they can be in any order
    /// @param start First link Pin
    /// @param end Second link Pin
    /// @param trigger_update If true, will trigger an update of the graph from start pin
    void CreateLink(Pin* start, Pin* end, const bool trigger_update);

    /// @brief Delete a Link from this App, will also delete it from the graph view
    /// @param id Link id
    void DeleteLink(const ax::NodeEditor::LinkId id);

    /// @brief Delete a Node from this App, will also delete it from the graph view
    /// @param id Node id
    void DeleteNode(const ax::NodeEditor::NodeId id);

    /// @brief Propagate rates updates from pin through the graph
    bool UpdateNodesRate(const Pin* pin, const FractionalNumber& new_rate);

    /// @brief Check for Arrow key inputs and nudge selected nodes if required
    void NudgeNodes();

    /// @brief Copy the position from the graph into the nodes struct
    void PullNodesPosition();

    /// @brief Bundle all selected nodes by a group node
    void GroupSelectedNodes();

    /// @brief Unpack all nodes contained in the currently selected node
    void UngroupSelectedNode();

    /// @brief Create a copy of all selected nodes, including internal links between them
    void DuplicateSelectedNodes();


    /// @brief Render the panel on the left with global info (inputs/outputs/etc...)
    void RenderLeftPanel();
    /// @brief Render the nodes in the main graph view
    void RenderNodes();
    /// @brief Render the links in the main graph view
    void RenderLinks();
    /// @brief Handle user dragging link to an empty space or another pin
    void DragLink();
    /// @brief React to user deleting nodes/links in the graph view
    void DeleteNodesLinks();
    /// @brief React to user wanting to create a new node (either by right clicking or dragging a Link in empty space)
    void AddNewNode();
    /// @brief Tooltips in the graph view are rendered in a second pass after everything else. Otherwise they are not at the right place
    void RenderTooltips();
    /// @brief Display a popup centered in the screen with all controls
    void RenderControlsPopup();
    /// @brief React to app-specific key pressed
    void CustomKeyControl();

private:
    /// @brief Used in saved files to track when format change. Used to update files saved with previous versions
    static constexpr int SAVE_VERSION = 5;

    /// @brief Window id used for the Add Node popup
    static constexpr std::string_view add_node_popup_id = "Add Node";
    /// @brief Folder to save/load the serialized graph
    static constexpr std::string_view save_folder = "saved";
    /// @brief Path used to save current session file
    static constexpr std::string_view session_file = "last_session.fcs";
    /// @brief Path used to save app settings
    static constexpr std::string_view settings_file = "settings.json";

    /// @brief All settings to customize app behaviour
    struct Settings {
        /// @brief If true, recipes marked as spoiler will not be proposed in the list
        bool hide_spoilers = true;
        /// @brief If true, somersloop override will not be displayed in the nodes
        bool hide_somersloop = false;
        /// @brief For each alt recipes, stores wether or not it's been unlocked yet
        std::map<const Recipe*, bool> unlocked_alts = {};
        /// @brief If true, will display power info with equal clocks on all machines in a node
        /// If false, it will compute the power for N machines at 100% + an underclocked machine
        bool power_equal_clocks = true;
        /// @brief If true, build progress bar and checkbox on craft nodes will be displayed
        bool show_build_progress = false;
    } settings;

    /// @brief All nodes currently in the graph view
    std::vector<std::unique_ptr<Node>> nodes;
    /// @brief All links currently in the graph view
    std::vector<std::unique_ptr<Link>> links;

    ax::NodeEditor::Config config;
    ax::NodeEditor::EditorContext* context;

    /// @brief Next available id for a node/link in the graph view
    unsigned long long int next_id;

    double last_time_saved_session;

    /* Values used during the rendering pass to save UI state between frames */
    std::string save_name;
    std::vector<std::pair<std::string, size_t>> file_suggestions;
    bool popup_opened;
    ImVec2 new_node_position;
    Pin* new_node_pin;
    std::string recipe_filter;
    std::vector<std::string> frame_tooltips;

    unsigned int somersloop_texture_id;

    std::chrono::steady_clock::time_point last_time_interacted;

    float error_time;
};
