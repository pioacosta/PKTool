#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Alphabetical views of the named option lists (species, moves, held items,
// natures, balls), so the editor and the pickers can step through them in
// name order instead of ID order.
//
// Sorting ignores case and accents (an accented e sorts as e) and
// punctuation. The views contain only the IDs that exist in the given
// generation (1-5); ID 0 ("(None)") is first in the lists that allow it.
// Views are built lazily, once per list and generation, as uint16_t vectors.
namespace pktool::data {

enum class NameKind : uint8_t { Species, Move, Item, Nature, Ball };

// Order of the views. Alphabetical is the default; ById lists the IDs in
// ascending order (the Settings screen chooses). The setting is global to the
// process and applies to sortedIds/sortedIndex/stepSorted/jumpSorted; in ById
// order jumpSorted moves ten entries instead of one initial letter.
enum class Order : uint8_t { Alphabetical, ById };
void setOrder(Order order);
Order currentOrder();

// Forgets the cached views (the names changed: the language was switched).
void resetSortedViews();

// Lowercase, accent-free, punctuation-free sort key of a name.
std::string foldName(const char* name);
// First letter/digit of a name, folded and lowercase ('~' if it has none).
uint8_t initialOf(const char* name);

// True when ID 0 is a valid value of the list (no move, no item).
bool allowsNone(NameKind kind);
// Display name of `id` for that list and generation.
const char* nameOf(NameKind kind, uint8_t format, uint16_t id);
// Valid IDs of the list in alphabetical order (None first when allowed).
const std::vector<uint16_t>& sortedIds(NameKind kind, uint8_t format);

// Position of `id` in the view, or -1 if it is not a valid value.
int sortedIndex(NameKind kind, uint8_t format, uint16_t id);
// ID `delta` places after `id` in alphabetical order, wrapping around. An ID
// outside the list goes to the first entry (delta > 0) or the last (delta < 0).
uint16_t stepSorted(NameKind kind, uint8_t format, uint16_t id, int delta);
// First ID of the next (direction > 0) or previous initial letter; None is a
// group of its own. Wraps around.
uint16_t jumpSorted(NameKind kind, uint8_t format, uint16_t id, int direction);

// Index of the first entry of the next/previous group of `count` entries,
// where initialAt(i) gives each entry's group (entries of a group are
// contiguous). Wraps around.
template <typename F>
int jumpGroup(int count, int index, int direction, F initialAt) {
    if (count <= 0) return 0;
    if (index < 0 || index >= count) return direction > 0 ? 0 : count - 1;
    auto groupStart = [&](int i) {
        const auto g = initialAt(i);
        while (i > 0 && initialAt(i - 1) == g) --i;
        return i;
    };
    if (direction > 0) {
        const auto g = initialAt(index);
        int i = index;
        while (i < count && initialAt(i) == g) ++i;
        return i >= count ? 0 : i;
    }
    int start = groupStart(index);
    if (start == 0) return groupStart(count - 1);
    return groupStart(start - 1);
}

// Sorts IDs by display name (same ordering as the views; ties keep ID order).
template <typename NameFn>
void sortIdsByName(std::vector<uint16_t>& ids, NameFn nameFn) {
    std::vector<std::pair<std::string, uint16_t>> keyed;
    keyed.reserve(ids.size());
    for (uint16_t id : ids) keyed.emplace_back(foldName(nameFn(id)), id);
    std::sort(keyed.begin(), keyed.end());
    for (size_t i = 0; i < ids.size(); ++i) ids[i] = keyed[i].second;
}

}  // namespace pktool::data
