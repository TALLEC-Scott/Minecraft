// Dumps every block, item, recipe and mob drop as JSON, straight from the
// game's own tables (item.h, mining.h, recipes.h, mob_drops.h), so the item
// wiki can't drift from the code. `uv run scripts/item_wiki.py` turns it into
// docs/wiki/Items.md (and optionally an HTML page).
//
//   g++ -std=c++20 -I include -I Libraries/include scripts/item_wiki.cpp -o scripts/item_wiki
//   ./scripts/item_wiki > items.json

#include <cstdio>
#include <string>

#include "cube.h"
#include "inventory_model.h"
#include "item.h"
#include "mining.h"
#include "mob_drops.h"
#include "recipes.h"

static const char* toolName(ToolKind k) {
    switch (k) {
    case ToolKind::Pickaxe:
        return "pickaxe";
    case ToolKind::Axe:
        return "axe";
    case ToolKind::Shovel:
        return "shovel";
    case ToolKind::Sword:
        return "sword";
    default:
        return "none";
    }
}

static const char* stepName(StepSound s) {
    static const char* names[] = {"none", "grass", "stone", "sand", "gravel", "snow", "wood", "water", "cloth"};
    int i = static_cast<int>(s);
    return (i >= 0 && i < 9) ? names[i] : "none";
}

// The wooden / stone tool of a kind (for break times with the right tool).
static ItemId toolOf(ToolKind kind, int tier) {
    for (ItemId id = FIRST_ITEM; id <= LAST_ITEM; ++id)
        if (toolKind(id) == kind && toolTier(id) == tier) return id;
    return ITEM_EMPTY;
}

static void printDrop(const ItemDrop& d) {
    if (d.count <= 0)
        std::printf("null");
    else
        std::printf("{\"id\": %d, \"name\": \"%s\", \"count\": %d}", d.id, itemName(d.id), d.count);
}

int main() {
    std::printf("{\n\"blocks\": [\n");
    bool first = true;
    for (int i = 1; i < BLOCK_TYPE_COUNT; i++) { // skip AIR
        auto t = static_cast<block_type>(i);
        BlockMining m = blockMining(t);
        uint32_t f = getBlockFlags(t);
        std::printf("%s  {\"id\": %d, \"name\": \"%s\", \"iconLayer\": %d,", first ? "" : ",\n", i, itemName(blockItem(t)),
                    itemIconLayer(blockItem(t)));
        first = false;
        std::printf(" \"solid\": %s, \"opaque\": %s, \"transparent\": %s, \"liquid\": %s, \"plant\": %s,",
                    f & BF_SOLID ? "true" : "false", f & BF_OPAQUE ? "true" : "false",
                    f & BF_TRANSPARENT ? "true" : "false", f & BF_LIQUID ? "true" : "false",
                    f & BF_CROSS ? "true" : "false");
        std::printf(" \"light\": %d, \"sound\": \"%s\", \"hardness\": %g, \"bestTool\": \"%s\", \"needsTool\": %s,",
                    getBlockLightEmission(t), stepName(getStepSound(t)), m.hardness, toolName(m.bestTool),
                    m.needsTool ? "true" : "false");
        ItemId wood = toolOf(m.bestTool, 1), stone = toolOf(m.bestTool, 2);
        std::printf(" \"ticksHand\": %d, \"ticksWood\": %d, \"ticksStone\": %d,", breakTicks(t, ITEM_EMPTY),
                    wood ? breakTicks(t, wood) : breakTicks(t, ITEM_EMPTY),
                    stone ? breakTicks(t, stone) : breakTicks(t, ITEM_EMPTY));
        // Drop with the right tool; roll 0 = the luckiest chance drop.
        ItemId best = stone ? stone : ITEM_EMPTY;
        std::printf(" \"drop\": ");
        printDrop(blockDrop(t, best, 0.5f));
        std::printf(", \"chanceDrop\": ");
        ItemDrop lucky = blockDrop(t, best, 0.0f), usual = blockDrop(t, best, 0.5f);
        if (lucky.id != usual.id || lucky.count != usual.count)
            printDrop(lucky);
        else
            std::printf("null");
        std::printf(", \"stack\": %d}", maxStackSize(blockItem(t)));
    }

    std::printf("\n],\n\"items\": [\n");
    for (ItemId id = FIRST_ITEM; id <= LAST_ITEM; ++id) {
        const ItemInfo* info = itemInfo(id);
        std::printf("%s  {\"id\": %d, \"name\": \"%s\", \"icon\": \"%s\", \"tool\": \"%s\", \"tier\": %d,", id == FIRST_ITEM ? "" : ",\n",
                    id, info->name, info->iconPath, toolName(info->tool), info->tier);
        std::printf(" \"damage\": %g, \"attackSpeed\": %g, \"hunger\": %d, \"saturation\": %g, \"stack\": %d}",
                    info->melee.damage, 1.0 / info->melee.cooldownSeconds, info->food.hunger, info->food.saturation,
                    maxStackSize(id));
    }
    std::printf("\n],\n\"fist\": {\"damage\": %g, \"attackSpeed\": %g},\n", FIST_MELEE.damage,
                1.0 / FIST_MELEE.cooldownSeconds);

    std::printf("\"recipes\": [\n");
    first = true;
    for (const Recipe& r : allRecipes()) {
        std::printf("%s  {\"width\": %d, \"height\": %d, \"cells\": [", first ? "" : ",\n", r.width, r.height);
        first = false;
        for (int i = 0; i < r.width * r.height; i++)
            std::printf("%s%s", i ? ", " : "",
                        r.cells[i] == ITEM_EMPTY ? "null" : ("\"" + std::string(itemName(r.cells[i])) + "\"").c_str());
        std::printf("], \"result\": ");
        printDrop(r.result);
        std::printf(", \"needsTable\": %s}", (r.width > 2 || r.height > 2) ? "true" : "false");
    }

    std::printf("\n],\n\"mobDrops\": [\n");
    static const char* MOB_NAMES[] = {"Pig", "Cow", "Sheep", "Chicken", "Zombie", "Skeleton", "Creeper", "Spider",
                                      "Creeper Cow"};
    static_assert(sizeof(MOB_NAMES) / sizeof(MOB_NAMES[0]) == MOB_TYPE_COUNT, "one name per MobType");
    first = true;
    for (int i = 0; i < MOB_TYPE_COUNT; i++) {
        auto type = static_cast<MobType>(i);
        ItemDrop lo = mobDrop(type, 0.0f), hi = mobDrop(type, 0.9999f);
        std::printf("%s  {\"mob\": \"%s\", \"item\": %s%s%s, \"min\": %d, \"max\": %d}", first ? "" : ",\n", MOB_NAMES[i],
                    lo.count ? "\"" : "", lo.count ? itemName(lo.id) : "null", lo.count ? "\"" : "", lo.count, hi.count);
        first = false;
    }
    std::printf("\n]\n}\n");
}
