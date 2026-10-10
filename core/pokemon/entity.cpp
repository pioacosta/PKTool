#include "core/pokemon/entity.h"

#include "core/pokemon/pk1.h"
#include "core/pokemon/pk2.h"
#include "core/pokemon/pk3.h"
#include "core/pokemon/pk4.h"
#include "core/pokemon/pk5.h"

namespace pktool::pokemon::entity {

size_t partySize(uint8_t format) {
    if (format == 1) return pk1::kPartySize;
    if (format == 2) return pk2::kPartySize;
    return format >= 5 ? pk5::kPartySize : format == 3 ? pk3::kPartySize : pk4::kPartySize;
}

size_t storedSize(uint8_t format) {
    if (format == 1) return pk1::kStoredSize;
    if (format == 2) return pk2::kStoredSize;
    return format == 3 ? pk3::kStoredSize : kStoredSize;
}

Pokemon decode(uint8_t format, const uint8_t* decrypted, size_t size) {
    if (format == 1) return pk1::toPokemon(decrypted, size);
    if (format == 2) return pk2::toPokemon(decrypted, size);
    if (format == 3) return pk3::toPokemon(decrypted, size == pk3::kPartySize ? size : pk3::kStoredSize);
    return format >= 5 ? pk5::toPokemon(decrypted, size) : pk4::toPokemon(decrypted, size);
}

void apply(const Pokemon& p, uint8_t* decrypted) {
    if (p.format == 1) {
        pk1::applyPokemon(p, decrypted);
    } else if (p.format == 2) {
        pk2::applyPokemon(p, decrypted);
    } else if (p.format == 3) {
        pk3::applyPokemon(p, decrypted);
    } else if (p.format >= 5) {
        pk5::applyPokemon(p, decrypted);
    } else {
        pk4::applyPokemon(p, decrypted);
    }
}

void refreshParty(uint8_t format, uint8_t* decrypted) {
    if (format == 1) {
        pk1::refreshPartyStats(decrypted);
    } else if (format == 2) {
        pk2::refreshPartyStats(decrypted);
    } else if (format == 3) {
        pk3::refreshPartyStats(decrypted);
    } else if (format >= 5) {
        pk5::refreshPartyStats(decrypted);
    } else {
        pk4::refreshPartyStats(decrypted);
    }
}

}  // namespace pktool::pokemon::entity
