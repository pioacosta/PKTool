#include "core/showdown/showdown.h"

#include <cstdlib>

#include "core/data/learn4.h"
#include "core/data/moves.h"
#include "core/data/names.h"

namespace pktool::showdown {

// Names are looked up up to Gen V; the builder rejects what the game lacks.
constexpr uint16_t kMaxItemGen5 = 638;
constexpr uint16_t kMaxAbilityGen5 = 164;
namespace {

// Lower-case letters and digits only; gender symbols become m/f, so
// "Nidoran♀", "Nidoran-F" and "nidoran f" all match.
std::string normalize(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c >= 'A' && c <= 'Z') {
            out += static_cast<char>(c - 'A' + 'a');
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            out += static_cast<char>(c);
        } else if (c == 0xE2 && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0x99) {
            unsigned char third = static_cast<unsigned char>(s[i + 2]);
            if (third == 0x80) out += 'f';  // ♀
            if (third == 0x82) out += 'm';  // ♂
            i += 2;
        } else if (c == 0xC3 && i + 1 < s.size()) {
            unsigned char next = static_cast<unsigned char>(s[i + 1]);
            if (next == 0xA9 || next == 0x89) out += 'e';  // é / É (Pokémon, Flabébé)
            ++i;
        }
    }
    return out;
}

template <typename NameFn>
uint16_t findIn(const std::string& name, int count, NameFn nameOf) {
    std::string wanted = normalize(name);
    if (wanted.empty()) return 0;
    for (int id = 1; id < count; ++id) {
        if (normalize(nameOf(static_cast<uint16_t>(id))) == wanted) return static_cast<uint16_t>(id);
    }
    return 0;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool startsWith(const std::string& s, const char* prefix) {
    return s.compare(0, std::char_traits<char>::length(prefix), prefix) == 0;
}

int statIndex(const std::string& token) {
    std::string t = normalize(token);
    if (t == "hp") return pokemon::kHp;
    if (t == "atk") return pokemon::kAtk;
    if (t == "def") return pokemon::kDef;
    if (t == "spa") return pokemon::kSpA;
    if (t == "spd") return pokemon::kSpD;
    if (t == "spe") return pokemon::kSpe;
    return -1;
}

// "252 Atk / 4 SpD / 252 Spe"
void parseStats(const std::string& text, uint8_t out[pokemon::kStatCount], int maxValue, Set& set) {
    size_t start = 0;
    while (start <= text.size()) {
        size_t slash = text.find('/', start);
        std::string part = trim(text.substr(start, slash == std::string::npos ? std::string::npos : slash - start));
        if (!part.empty()) {
            size_t space = part.find(' ');
            int value = std::atoi(part.substr(0, space).c_str());
            int stat = space == std::string::npos ? -1 : statIndex(part.substr(space + 1));
            if (stat < 0) {
                set.warnings.push_back("Ignored stat \"" + part + "\"");
            } else {
                out[stat] = static_cast<uint8_t>(value < 0 ? 0 : (value > maxValue ? maxValue : value));
            }
        }
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
}

// "Nickname (Species) (M) @ Item", "Species (F) @ Item", "Species @ Item".
void parseFirstLine(const std::string& line, Set& set) {
    set.title = line;
    std::string head = line;
    size_t at = head.find(" @ ");
    if (at != std::string::npos) {
        std::string item = trim(head.substr(at + 3));
        set.item = findItem(item);
        if (set.item == 0) set.warnings.push_back("Unknown item \"" + item + "\"");
        head = trim(head.substr(0, at));
    }
    // Gender marker at the end.
    if (head.size() >= 4 && head[head.size() - 1] == ')' && head[head.size() - 3] == '(' &&
        (head[head.size() - 2] == 'M' || head[head.size() - 2] == 'F')) {
        set.gender = head[head.size() - 2] == 'M' ? static_cast<int>(pokemon::Gender::Male)
                                                  : static_cast<int>(pokemon::Gender::Female);
        head = trim(head.substr(0, head.size() - 3));
    }
    std::string speciesText = head;
    if (!head.empty() && head.back() == ')') {
        size_t open = head.rfind('(');
        if (open != std::string::npos) {
            speciesText = head.substr(open + 1, head.size() - open - 2);
            set.nickname = trim(head.substr(0, open));
        }
    }
    set.species = findSpecies(speciesText);
    if (set.species == 0) {
        // Forms such as "Rotom-Wash" or "Deoxys-Speed": use the base species.
        size_t dash = speciesText.rfind('-');
        if (dash != std::string::npos) {
            set.species = findSpecies(speciesText.substr(0, dash));
            if (set.species != 0) set.warnings.push_back("Form \"" + speciesText + "\" imported as its base form");
        }
    }
    if (set.species == 0) set.warnings.push_back("Unknown species \"" + speciesText + "\"");
}

void parseLine(const std::string& line, Set& set, int& moveCount) {
    if (line[0] == '-') {
        std::string name = trim(line.substr(1));
        uint16_t move = findMove(name);
        if (move == 0 && startsWith(normalize(name), "hiddenpower")) move = 237;  // "Hidden Power [Fire]"
        if (move == 0) {
            set.warnings.push_back("Unknown move \"" + name + "\"");
        } else if (moveCount < 4) {
            set.moves[moveCount++] = move;
        }
        return;
    }
    if (startsWith(line, "Ability:")) {
        std::string name = trim(line.substr(8));
        set.ability = findAbility(name);
        if (set.ability == 0) set.warnings.push_back("Unknown ability \"" + name + "\"");
    } else if (startsWith(line, "Level:")) {
        int level = std::atoi(line.c_str() + 6);
        set.level = static_cast<uint8_t>(level < 1 ? 1 : (level > 100 ? 100 : level));
    } else if (startsWith(line, "Shiny:")) {
        set.shiny = normalize(line.substr(6)) == "yes";
    } else if (startsWith(line, "Happiness:")) {
        int value = std::atoi(line.c_str() + 10);
        set.friendship = static_cast<uint8_t>(value < 0 ? 0 : (value > 255 ? 255 : value));
    } else if (startsWith(line, "EVs:")) {
        parseStats(line.substr(4), set.evs, 255, set);
    } else if (startsWith(line, "IVs:")) {
        parseStats(line.substr(4), set.ivs, 31, set);
    } else if (line.size() > 7 && line.compare(line.size() - 7, 7, " Nature") == 0) {
        std::string name = line.substr(0, line.size() - 7);
        set.nature = findNature(name);
        if (set.nature < 0) set.warnings.push_back("Unknown nature \"" + name + "\"");
    } else if (startsWith(line, "Tera Type:") || startsWith(line, "Gigantamax:") || startsWith(line, "Dynamax Level:") ||
               startsWith(line, "Pokeball:") || startsWith(line, "Hidden Power:")) {
        // Later-generation fields: not applicable to Gen IV.
    } else {
        set.warnings.push_back("Ignored line \"" + line + "\"");
    }
}

}  // namespace

uint16_t findSpecies(const std::string& name) {
    return findIn(name, data::kMaxSpeciesGen5 + 1, [](uint16_t id) { return data::speciesNameEn(id); });
}

uint16_t findMove(const std::string& name) {
    return findIn(name, data::kMaxMoveGen5 + 1, [](uint16_t id) { return data::moveNameEn(id); });
}

uint16_t findItem(const std::string& name) {
    return findIn(name, kMaxItemGen5 + 1, [](uint16_t id) { return data::itemNameEn(id); });
}

uint16_t findAbility(const std::string& name) {
    return findIn(name, kMaxAbilityGen5 + 1, [](uint16_t id) { return data::abilityNameEn(id); });
}

int findNature(const std::string& name) {
    std::string wanted = normalize(name);
    for (int n = 0; n < 25; ++n) {
        if (normalize(data::natureNameEn(static_cast<uint8_t>(n))) == wanted) return n;
    }
    return -1;
}

std::vector<Set> parse(const std::string& text) {
    std::vector<Set> sets;
    Set current;
    bool inSet = false;
    int moveCount = 0;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        std::string line = trim(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (line.empty()) {
            if (inSet) {
                sets.push_back(current);
                current = Set();
                inSet = false;
                moveCount = 0;
            }
        } else if (line[0] == '=' || startsWith(line, "//")) {
            // Team headers ("=== [gen4] Team ===") and comments.
        } else if (!inSet) {
            parseFirstLine(line, current);
            inSet = true;
        } else {
            parseLine(line, current, moveCount);
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (inSet) sets.push_back(current);
    return sets;
}

namespace {

// Gen I/II: no ability, nature or HP IV; DVs print as IVs (x2, a full 15 is 31),
// Stat Exp as EVs (its square root, 255 at most).
std::string formatGb(const pokemon::Pokemon& p) {
    static const int kOrder[] = {pokemon::kAtk, pokemon::kDef, pokemon::kSpA, pokemon::kSpD, pokemon::kSpe};
    static const char* const kLabels[] = {"Atk", "Def", "SpA", "SpD", "Spe"};
    std::string species = data::speciesNameEn(p.species);
    std::string out = p.isNicknamed && p.nickname != species ? p.nickname + " (" + species + ")" : species;
    if (p.format == 2 && p.gender == pokemon::Gender::Male) out += " (M)";
    if (p.format == 2 && p.gender == pokemon::Gender::Female) out += " (F)";
    if (p.format == 2 && p.heldItem != 0) out += std::string(" @ ") + data::itemNameEn(p.heldItem);
    out += "\n";
    if (p.level != 100) out += "Level: " + std::to_string(p.level) + "\n";
    if (p.shiny && p.format == 2) out += "Shiny: Yes\n";
    if (p.format == 2) out += "Happiness: " + std::to_string(p.friendship) + "\n";
    std::string evs;
    std::string ivs;
    for (int i = 0; i < 5; ++i) {
        const int s = kOrder[i];
        unsigned root = 0;
        while (root < 255 && root * root < p.statExp[s]) ++root;
        if (root != 0) evs += (evs.empty() ? "" : " / ") + std::to_string(root) + " " + kLabels[i];
        const int iv = p.ivs[s] * 2 + (p.ivs[s] == 15 ? 1 : 0);
        if (iv != 31) ivs += (ivs.empty() ? "" : " / ") + std::to_string(iv) + " " + kLabels[i];
    }
    if (!evs.empty()) out += "EVs: " + evs + "\n";
    if (!ivs.empty()) out += "IVs: " + ivs + "\n";
    for (uint16_t move : p.moves) {
        if (move != 0) out += std::string("- ") + data::moveNameEn(move) + "\n";
    }
    return out;
}

}  // namespace

std::string format(const pokemon::Pokemon& p) {
    if (p.format <= 2) return formatGb(p);
    // Showdown's stat order and labels.
    static const int kOrder[pokemon::kStatCount] = {pokemon::kHp, pokemon::kAtk, pokemon::kDef,
                                                    pokemon::kSpA, pokemon::kSpD, pokemon::kSpe};
    static const char* const kLabels[pokemon::kStatCount] = {"HP", "Atk", "Def", "SpA", "SpD", "Spe"};
    std::string species = data::speciesNameEn(p.species);
    std::string out = p.isNicknamed && p.nickname != species ? p.nickname + " (" + species + ")" : species;
    if (p.gender == pokemon::Gender::Male) out += " (M)";
    if (p.gender == pokemon::Gender::Female) out += " (F)";
    if (p.heldItem != 0) out += std::string(" @ ") + data::itemNameEn(p.heldItem);
    out += "\n";
    out += std::string("Ability: ") + data::abilityNameEn(p.ability) + "\n";
    if (p.level != 100) out += "Level: " + std::to_string(p.level) + "\n";
    if (p.shiny) out += "Shiny: Yes\n";
    if (p.friendship != 255) out += "Happiness: " + std::to_string(p.friendship) + "\n";
    std::string evs;
    std::string ivs;
    for (int i = 0; i < pokemon::kStatCount; ++i) {
        int s = kOrder[i];
        if (p.evs[s] != 0) evs += (evs.empty() ? "" : " / ") + std::to_string(p.evs[s]) + " " + kLabels[i];
        if (p.ivs[s] != 31) ivs += (ivs.empty() ? "" : " / ") + std::to_string(p.ivs[s]) + " " + kLabels[i];
    }
    if (!evs.empty()) out += "EVs: " + evs + "\n";
    out += std::string(data::natureNameEn(p.nature)) + " Nature\n";
    if (!ivs.empty()) out += "IVs: " + ivs + "\n";
    for (uint16_t move : p.moves) {
        if (move != 0) out += std::string("- ") + data::moveNameEn(move) + "\n";
    }
    return out;
}

}  // namespace pktool::showdown
