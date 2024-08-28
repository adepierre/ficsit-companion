#pragma once

#include <memory>
#include <queue>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <imgui_node_editor.h>

#include "recipe.hpp"

struct Link;
struct Node;
struct CraftNode;
struct Pin;

class App
{
public:
    App();
    ~App();
    void Render();

public:
    /// @brief Save current session (should NOT require an active ImGui context)
    void SaveSession();

private:
    /// @brief Load saved session if present
    void LoadSession();

    /// @brief Serialize the app state to a string
    /// @return Serialized state of this app
    std::string Serialize() const;

    /// @brief Restore app state from a string
    /// @param s Serialized app state to load
    void Deserialize(const std::string& s);

    /// @brief Get the next available id for node-editor
    /// @return The next id to use
    unsigned long long int GetNextId();

    /// @brief Load recipes from "recipes.json"
    void LoadRecipes();

    /// @brief Search for a Pin given its id
    /// @param id The pin id
    /// @return A pointer to the Pin, nullptr if not found
    Pin* FindPin(ax::NodeEditor::PinId id) const;

    /// @brief Create a link between two pins, they can be in any order
    /// @param start First link Pin
    /// @param end Second link Pin
    void CreateLink(Pin* start, Pin* end);

    /// @brief Delete a Link from this App, will also delete it from the graph view
    /// @param id Link id
    void DeleteLink(const ax::NodeEditor::LinkId id);

    /// @brief Delete a Node from this App, will also delete it from the graph view
    /// @param id Node id
    void DeleteNode(const ax::NodeEditor::NodeId id);

    /// @brief Propagate rates updates from updating_pins through the graph
    void UpdateNodesRate();

    /// @brief Check for Arrow key inputs and nudge selected nodes if required
    void NudgeNodes();

    /// @brief Copy the position from the graph into the nodes struct
    void PullNodesPosition();


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
    static constexpr int SAVE_VERSION = 2;

    /// @brief Window id used for the Add Node popup
    static constexpr std::string_view add_node_popup_id = "Add Node";
    /// @brief Folder to save/load the serialized graph
    static constexpr std::string_view save_folder = "saved";
    /// @brief Path used to save current session file
    static constexpr std::string_view session_file = "last_session.fcs";


    /// @brief Version of the game the items/recipes are from
    std::string recipes_version;
    /// @brief All known items
    std::unordered_map<std::string, std::unique_ptr<Item>> items;
    /// @brief All known recipes
    std::vector<Recipe> recipes;

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

    enum class Constraint { None, Weak, Strong };
    /// @brief All pins which had their value changed and need to propagate updates
    std::queue<std::pair<const Pin*, Constraint>> updating_pins;

};
