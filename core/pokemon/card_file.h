#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "core/data/events4.h"
#include "core/data/events5.h"

// Wonder Card files found on the SD card (/PKTOOL/events/), parsed into the
// same structures the compiled tables use, so core/pokemon/gift4 and gift5
// receive them like a table card.
//
//   .pgt  Gen IV gift (PKHeX PGT, 260 bytes): type, slot and a PK4 template
//   .pcd  Gen IV Wonder Card (PCD, 856 bytes): a PGT plus the card title and ID
//   .pgf  Gen V Wonder Card (PGF, 204 bytes)
//
// There is no Gen III counterpart: PKHeX has no Pokémon gift file for Gen III
// (its e-Reader Wonder Cards carry no Pokémon), so .wc3 files are not read.
namespace pktool::pokemon::cardfile {

enum class Status : uint8_t {
    Ok,          // a Pokémon card that can be received
    NotPokemon,  // a valid card that is not a receivable Pokémon (item, rule, egg...)
    Invalid,     // not a card of its type: wrong size, bad checksum, impossible species
};

// Extensions of the files in the events folder (lower case, with the dot).
constexpr const char* kExtensions[] = {".pgt", ".pcd", ".pgf"};
constexpr int kExtensionCount = 3;

struct Card {
    Status status = Status::Invalid;
    uint8_t generation = 0;  // 4 or 5
    std::string title;       // card title (file name when the file has none)
    std::string fileName;
    std::string reason;      // why it is not Ok ("Not a Pokémon card", ...)
    uint16_t species = 0;    // 0 unless Ok
    // The card in the structures the compiled tables use (valid when Ok;
    // event4 or event5 by `generation`). Their `title` points to `title`
    // above, so a Card stays where it was created (parse returns a unique_ptr).
    data::Event4 event4{};
    data::Event5 event5{};
    // Index of the identical compiled table card, -1 if none. A file that is
    // a table card borrows its distribution periods (and so passes the same
    // met-date check); any other card has none and is dated "today".
    int tableIndex = -1;

    Card() = default;
    Card(const Card&) = delete;
    Card& operator=(const Card&) = delete;
};

// Parses one file's bytes. `extension` is ".pgt", ".pcd" or ".pgf" (any case)
// and `fileName` only names the card. Never returns null.
std::unique_ptr<Card> parse(const uint8_t* bytes, size_t size, const std::string& extension,
                            const std::string& fileName);

}  // namespace pktool::pokemon::cardfile
