#pragma once
#include "json.hpp"

#include <string>

struct Item;
struct Recipe;

unsigned int LoadTextureFromFile(const std::string& path);

/// @brief Update the given save to a given version
/// @param save Save Json to update
/// @param to Destination save version
/// @return True if the save was correctly updated, false otherwise
bool UpdateSave(Json::Value& save, const int to);

struct ItemPtrCompare {
    bool operator()(const Item* a, const Item* b) const;
};

struct RecipePtrCompare {
    bool operator()(const Recipe* a, const Recipe* b) const;
};
