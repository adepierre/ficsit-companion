#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "building.hpp"
#include "game_data.hpp"
#include "json.hpp"
#include "recipe.hpp"

namespace Data
{
    namespace // anonymous namespace to store the game data
    {
        std::string version;
        std::unordered_map<std::string, std::unique_ptr<Item>> items;
        std::unordered_map<std::string, std::unique_ptr<Building>> buildings;
        std::vector<std::unique_ptr<Recipe>> recipes;
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
            buildings[name] = std::make_unique<Building>(
                name,
                FractionalNumber(std::to_string(b["somersloop_mult"].get<double>())),
                b["power"].get<double>(),
                b["power_exponent"].get<double>(),
                b["somersloop_power_exponent"].get<double>(),
                b["variable_power"].get<bool>()
            );
        }

        for (const auto& i : data["items"].get_array())
        {
            const std::string& name = i["name"].get_string();
            items[name] = std::make_unique<Item>(name, i["icon"].get_string(), i["sink"].get_number<int>());
        }

        const Json::Array& json_recipes = data["recipes"].get_array();

        for (const auto& r : json_recipes)
        {
            const FractionalNumber time = r["time"].is<double>() ?
                FractionalNumber(std::to_string(r["time"].get<double>())) :
                FractionalNumber(r["time"].get_string());
            std::vector<CountedItem> inputs;
            for (const auto& i : r["inputs"].get_array())
            {
                inputs.emplace_back(CountedItem(items.at(i["name"].get_string()).get(), FractionalNumber(std::to_string(i["amount"].get<double>() * 60.0)) / time));
            }
            std::vector<CountedItem> outputs;
            for (const auto& o : r["outputs"].get_array())
            {
                outputs.emplace_back(CountedItem(items.at(o["name"].get_string()).get(), FractionalNumber(std::to_string(o["amount"].get<double>() * 60.0)) / time));
            }

            const Building* building = buildings.at(r["building"].get_string()).get();
            recipes.emplace_back(std::make_unique<Recipe>(
                inputs,
                outputs,
                building,
                r["alternate"].get<bool>(),
                (r.contains("power_constant") && r.contains("power_range")) ? r["power_constant"].get<double>() + 0.5 * r["power_range"].get<double>() : building->power,
                r["name"].get_string(),
                r.contains("spoiler") && r["spoiler"].get<bool>()
            ));
        }

        std::stable_sort(recipes.begin(), recipes.end(), [](const std::unique_ptr<Recipe>& a, const std::unique_ptr<Recipe>& b) {
            return a->name < b->name;
        });
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

    const std::vector<std::unique_ptr<Recipe>>& Recipes()
    {
        return recipes;
    }
}
