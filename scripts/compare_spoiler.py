import json

def compare_arrays_of_dict(old, new, name_key="name"):
    added = []
    removed = []
    changed = []
    for d in old:
        matched = [n for n in new if d[name_key] == n[name_key]]
        if not matched:
            removed.append(d[name_key])
        elif matched[0] != d:
            changed.append(d[name_key])
    for d in new:
        matched = [n for n in old if d[name_key] == n[name_key]]
        if not matched:
            added.append(d[name_key])

    print("\tRemoved:")
    for s in removed:
        print("\t\t", s)
    print("\tAdded:")
    for s in added:
        print("\t\t", s)
    print("\tChanged:")
    for s in changed:
        print("\t\t", s)


OLD = "satisfactory_old.json"
NEW = "satisfactory.json"

with open(OLD, "r") as f:
    old = json.load(f)
with open(NEW, "r") as f:
    new = json.load(f)

print("Buildings:")
compare_arrays_of_dict(old["buildings"], new["buildings"])

print("Items:")
compare_arrays_of_dict(old["items"], new["items"])

print("Recipes:")
compare_arrays_of_dict(old["recipes"], new["recipes"])
