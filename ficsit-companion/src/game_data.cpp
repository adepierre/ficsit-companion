#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "game_data.hpp"
#include "json.hpp"

namespace Data
{
    namespace // anonymous namespace to store the game data
    {
        std::string version;
        std::unordered_map<std::string, std::unique_ptr<Item>> items;
        std::unordered_map<std::string, std::unique_ptr<Building>> buildings;
        std::vector<Recipe> recipes;
    }

    void LoadData(const std::string& game)
    {
        if (!std::filesystem::exists(game + ".json"))
        {
            throw std::runtime_error("Data file not found for game " + game);
        }

        items.clear();
        recipes.clear();
        version = "";

        std::ifstream f(game + ".json");
        Json::Value data;
        f >> data;
        f.close();

        version = data["version"].get_string();

        for (const auto& b : data["buildings"].get_array())
        {
            const std::string& name = b["name"].get_string();
            buildings[name] = std::make_unique<Building>(name, FractionalNumber(std::to_string(b["somersloop_mult"].get<double>())));
        }

        for (const auto& i : data["items"].get_array())
        {
            const std::string& name = i["name"].get_string();
            items[name] = std::make_unique<Item>(name, i["icon"].get_string());
        }

        // Trick to sort the recipes alphabetically using std::map default sorting
        Json::Value ordered_recipes;
        for (const auto& r : data["recipes"].get_array())
        {
            ordered_recipes[r["name"].get_string()] = r;
        }

        for (const auto& [name, r] : ordered_recipes.get_object())
        {
            const int time = static_cast<int>(r["time"].get<double>());
            std::vector<CountedItem> inputs;
            for (const auto& i : r["inputs"].get_array())
            {
                inputs.emplace_back(CountedItem(items.at(i["name"].get_string()).get(), FractionalNumber(std::to_string(i["amount"].get<double>() * 60.0) + "/" + std::to_string(time))));
            }
            std::vector<CountedItem> outputs;
            for (const auto& o : r["outputs"].get_array())
            {
                outputs.emplace_back(CountedItem(items.at(o["name"].get_string()).get(), FractionalNumber(std::to_string(o["amount"].get<double>() * 60.0) + "/" + std::to_string(time))));
            }
            recipes.emplace_back(Recipe(
                inputs,
                outputs,
                buildings.at(r["building"].get_string()).get(),
                r["alternate"].get<bool>(),
                name,
                r.contains("spoiler") && r["spoiler"].get<bool>()
            ));
        }
    }

    const std::string& Version()
    {
        return version;
    }

    const std::unordered_map<std::string, std::unique_ptr<Item>>& Items()
    {
        return items;
    }

    const std::unordered_map<std::string, std::unique_ptr<Building>>& Buildings()
    {
        return buildings;
    }

    const std::vector<Recipe>& Recipes()
    {
        return recipes;
    }
}
