#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/pokemon/pk1.h"
#include "core/pokemon/pk2.h"

// The Time Capsule (Gen I <-> Gen II trades), as the Gold/Silver/Crystal games
// do it (PKHeX PK1.ConvertToPK2 / PK2.ConvertToPK1). It works on the PK1/PK2
// entity blobs and never on raw bytes of another format.
//
//  * Gen I -> II: the catch rate byte becomes the held item (a few rates are
//    other items, see data::itemFromCatchRate), friendship starts at the
//    species' base value, Crystal's caught data and Pokerus are empty. The
//    shiny DVs make it shiny.
//  * Gen II -> I: only species up to #151, knowing only moves up to #165 and
//    not an egg may go back; the held item becomes the catch rate (0 for no
//    item), the types are the species', friendship, Pokerus and Crystal's
//    caught data are lost. Korean Pokemon cannot (there are no Korean Gen I games).
//
// Japanese and international games cannot trade with each other at all, so
// a Pokemon keeps its language blob byte and callers must refuse mixing them.
namespace pktool::pokemon::timecapsule {

struct Result {
    bool ok = false;
    std::string reason;  // why a Pokemon cannot go (when !ok)
};

// Result::ok is false when the Pokemon cannot take the trip; `out` is then not
// written. `notes` receives what changed that the player may care about.
Result toGen2(const uint8_t pk1[pk1::kEntitySize], uint8_t pk2Out[pk2::kEntitySize],
              std::vector<std::string>* notes = nullptr);
Result toGen1(const uint8_t pk2[pk2::kEntitySize], uint8_t pk1Out[pk1::kEntitySize],
              std::vector<std::string>* notes = nullptr);

}  // namespace pktool::pokemon::timecapsule
