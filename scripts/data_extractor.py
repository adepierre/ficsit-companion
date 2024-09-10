import os, json, re, shutil

from typing import List, Dict

DOCS_PATH = "Docs.json"

RECIPES = ["/Script/CoreUObject.Class'/Script/FactoryGame.FGRecipe'"]
BUILDINGS = [
    "/Script/CoreUObject.Class'/Script/FactoryGame.FGBuildableManufacturer'",
    "/Script/CoreUObject.Class'/Script/FactoryGame.FGBuildableManufacturerVariablePower'"
]
ITEMS = [
    "/Script/CoreUObject.Class'/Script/FactoryGame.FGItemDescriptor'",
    "/Script/CoreUObject.Class'/Script/FactoryGame.FGResourceDescriptor'",
    "/Script/CoreUObject.Class'/Script/FactoryGame.FGItemDescriptorBiomass'",
    "/Script/CoreUObject.Class'/Script/FactoryGame.FGConsumableDescriptor'",
    "/Script/CoreUObject.Class'/Script/FactoryGame.FGItemDescriptorNuclearFuel'",
    "/Script/CoreUObject.Class'/Script/FactoryGame.FGEquipmentDescriptor'",
    "/Script/CoreUObject.Class'/Script/FactoryGame.FGAmmoTypeProjectile'",
    "/Script/CoreUObject.Class'/Script/FactoryGame.FGAmmoTypeInstantHit'",
    "/Script/CoreUObject.Class'/Script/FactoryGame.FGAmmoTypeSpreadshot'",
    "/Script/CoreUObject.Class'/Script/FactoryGame.FGPowerShardDescriptor'",
    "/Script/CoreUObject.Class'/Script/FactoryGame.FGItemDescriptorPowerBoosterFuel'",
]

def get_classes(doc, keys: List[str]):
    output = []
    for element in doc:
        if element["NativeClass"] in keys:
            output += element["Classes"]
    if len(output) == 0:
        raise RuntimeError(f"Can't find any item in doc matching {keys}")
    return output

def get_building(recipe: Dict, buildings: Dict):
    building_regex = re.compile(r"\"(?:.*?\.)(.*?)\"")
    match_buildings = [m.group(1) for m in re.finditer(building_regex, recipe["mProducedIn"]) if not "Workshop" in m.group(1) and not "WorkBench" in m.group(1)]
    if len(match_buildings) == 0:
        return None
    if len(match_buildings) > 1:
        raise RuntimeError(f"Unclear building for recipe {recipe['mDisplayName']}")
    if not match_buildings[0] in buildings:
        raise RuntimeError(f"Unknown building for recipe {recipe['mDisplayName']}")
    return buildings[match_buildings[0]]

def parse_counted_item_list(s: str, items: Dict):
    item_regex = re.compile(r"\(ItemClass=.*?\.([^.]*)'\",Amount=([0-9.]+)\)")
    parsed = []
    for m in re.finditer(item_regex, s):
        item_classname = m.group(1)
        if not item_classname in items:
            raise RuntimeError(f"Unknown item {item_classname}")
        parsed.append({
            "name": items[item_classname]["name"],
            "amount": float(m.group(2)) / (1.0 if items[item_classname]["state"] == "RF_SOLID" else 1000.0)
        })
    return parsed

with open(DOCS_PATH, "r", encoding="utf-16") as f:
    data = json.load(f)

buildings = {
    b["ClassName"]: {
        "name": b["mDisplayName"],
        # TODO: add power data, need special cases for Particle Accelerator and potentially 1.0 quantum buildings (variable power + recipe dependant)
    } for b in get_classes(data, BUILDINGS)
}

items = {
    c["ClassName"]: {
        "name": c["mDisplayName"],
        "icon": c["mSmallIcon"],
        "state": c["mForm"]
    } for c in get_classes(data, ITEMS)
}

recipes = {}
for recipe in get_classes(data, RECIPES):
    if recipe["mProducedIn"] == "" or "BuildGun" in recipe["mProducedIn"] or recipe["mRelevantEvents"] != "" or "Fireworks" in recipe["FullName"]:
        continue
    building: Dict = get_building(recipe, buildings)
    if building is None:
        continue
    inputs: List[Dict] = []
    outputs: List[Dict] = []

    recipes[recipe["ClassName"]] = {
        "name": recipe["mDisplayName"].replace("Alternate:", "").strip(),
        "alternate": "Alternate" in recipe["mDisplayName"],
        "time": float(recipe["mManufactoringDuration"]),
        "building": building["name"],
        "inputs": parse_counted_item_list(recipe["mIngredients"], items),
        "outputs": parse_counted_item_list(recipe["mProduct"], items)
    }

# Copy icons for each used item
if os.path.exists("icons"):
    shutil.rmtree("icons")
os.makedirs("icons")
removed = []
for v in items.values():
    used = False
    for recipe in recipes.values():
        for item in recipe["inputs"] + recipe["outputs"]:
            if item["name"] == v["name"]:
                used = True
                break
        if used:
            break
    if not used:
        removed.append(v)
        continue
    relative_path = v["icon"].replace("Texture2D /Game/", "")
    folder_path = os.path.dirname(relative_path)
    icon_start_name = "_".join(os.path.basename(relative_path).split(".")[1].split("_")[:-1])
    min_resolution = [int(s) for s in relative_path.split("_") if s.isdecimal()][0]
    # Find the smallest image for this item
    min_res_file = os.path.basename(relative_path).split(".")[0] + ".png"
    for file in os.listdir(folder_path):
        if file.startswith(icon_start_name):
            current_res = [int(s) for s in file.split(".")[0].split("_") if s.isdecimal()][0]
            if current_res < min_resolution:
                min_resolution = current_res
                min_res_file = file
    if not os.path.exists(os.path.join(folder_path, min_res_file)):
        print("WARNING, path not found", os.path.join(folder_path, min_res_file))
        v["icon"] = ""
        continue
    shutil.copy(os.path.join(folder_path, min_res_file), os.path.join("icons", min_res_file))
    v["icon"] = "icons/" + min_res_file

# Manually copy somersloop icon
shutil.copy("FactoryGame/Prototype/WAT/UI/Wat_1_64.png", "icons/Wat_1_64.png")

with open("satisfactory.json", "w") as out_file:
    json.dump({
        "version": "",
        "buildings": list(buildings.values()),
        "items": [ { "name": v["name"], "icon": v["icon"] } for v in items.values() if not v in removed],
        "recipes": list(recipes.values())
    }, out_file, indent=4, ensure_ascii=False)

print("Done! Don't forget to update version in the resulting json file")
