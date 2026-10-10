#include "core/saves/gb_save.h"

#include <algorithm>
#include <cstring>

#include "core/text/gen12_text.h"

namespace pktool::saves {

bool GbSave::sizeOk(size_t size) {
    for (size_t base : {size_t{0x8000}, size_t{0x10000}}) {
        if (size < base) continue;
        const size_t footer = size - base;
        if (footer == 0 || footer == 7 || (footer >= 0x0C && footer <= kMaxTrailer && (footer & 1) == 0)) return true;
    }
    return false;
}

bool GbSave::allZero(const uint8_t* bytes, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        if (bytes[i] != 0) return false;
    }
    return true;
}

size_t GbSave::listLength(bool party) const {
    const int capacity = party ? kPartyMax : boxCapacity_;
    const size_t body = party ? geometry_.bodyParty : geometry_.bodyStored;
    return 2 + (geometry_.stringBytes * 2 + body + 1) * static_cast<size_t>(capacity);
}

std::string GbSave::decodeName(const uint8_t* field, size_t length) const {
    return text::decodeGb(geometry_.format, field, length, language());
}

bool GbSave::encodeName(const std::string& name, uint8_t* field, size_t length, size_t maxChars) const {
    return text::encodeGb(geometry_.format, name, field, length, maxChars, language());
}

void GbSave::unpackList(const uint8_t* list, bool party, Slot* out, int capacity) const {
    const size_t body = party ? geometry_.bodyParty : geometry_.bodyStored;
    const size_t str = geometry_.stringBytes;
    const size_t otOffset = geometry_.bodyParty;
    const size_t nickOffset = otOffset + 11;
    const size_t languageOffset = nickOffset + 11;
    const size_t bodies = 1 + static_cast<size_t>(capacity) + 1;
    const size_t ots = bodies + body * static_cast<size_t>(capacity);
    const size_t nicks = ots + str * static_cast<size_t>(capacity);
    const int count = std::min<int>(capacity, list[0]);
    for (int i = 0; i < capacity; ++i) out[i] = Slot();
    for (int i = 0; i < count; ++i) {
        Slot& s = out[i];
        s.marker = list[1 + i];
        if (!s.present()) continue;
        std::memcpy(s.blob, list + bodies + body * static_cast<size_t>(i), body);
        std::memset(s.blob + otOffset, text::kGb12Terminator, 22);
        std::memcpy(s.blob + otOffset, list + ots + str * static_cast<size_t>(i), str);
        std::memcpy(s.blob + nickOffset, list + nicks + str * static_cast<size_t>(i), str);
        s.blob[languageOffset] = language();
        if (geometry_.format == 2) s.blob[languageOffset + 1] = s.marker == 0xFD ? 1 : 0;
    }
}

bool GbSave::packList(uint8_t* list, bool party, const Slot* slots, bool destInitialised) {
    const int capacity = party ? kPartyMax : boxCapacity_;
    const size_t body = party ? geometry_.bodyParty : geometry_.bodyStored;
    const size_t str = geometry_.stringBytes;
    const size_t otOffset = geometry_.bodyParty;
    const size_t nickOffset = otOffset + 11;
    const size_t bodies = 1 + static_cast<size_t>(capacity) + 1;
    const size_t ots = bodies + body * static_cast<size_t>(capacity);
    const size_t nicks = ots + str * static_cast<size_t>(capacity);
    int count = 0;
    for (int i = 0; i < capacity; ++i) count += slots[i].present() ? 1 : 0;
    if (count == 0 && (!destInitialised || allZero(list, listLength(party)))) return false;
    int index = 0;
    for (int i = 0; i < capacity; ++i) {
        if (!slots[i].present()) continue;
        list[1 + index] = slots[i].marker;
        std::memcpy(list + bodies + body * static_cast<size_t>(index), slots[i].blob, body);
        std::memcpy(list + ots + str * static_cast<size_t>(index), slots[i].blob + otOffset, str);
        std::memcpy(list + nicks + str * static_cast<size_t>(index), slots[i].blob + nickOffset, str);
        ++index;
    }
    for (; index < capacity; ++index) {  // vacated entries: empty marker, zeroed data
        list[1 + index] = 0xFF;
        std::memset(list + bodies + body * static_cast<size_t>(index), 0, body);
        std::memset(list + ots + str * static_cast<size_t>(index), 0, str);
        std::memset(list + nicks + str * static_cast<size_t>(index), 0, str);
    }
    list[0] = static_cast<uint8_t>(count);
    list[1 + capacity] = 0xFF;
    return true;
}

void GbSave::unpackAll() {
    boxCount_ = geometry_.boxCount;
    boxCapacity_ = geometry_.boxCapacity;
    boxes_.assign(static_cast<size_t>(boxCount_) * static_cast<size_t>(boxCapacity_), Slot());
    boxDirty_.assign(static_cast<size_t>(boxCount_), false);
    allBoxesDirty_ = false;
    partyDirty_ = false;
    const int current = currentBox();
    for (int b = 0; b < boxCount_; ++b) {
        Slot* slots = &boxes_[static_cast<size_t>(b) * static_cast<size_t>(boxCapacity_)];
        if (b == current && readsCurrentBoxFromMirror()) {
            unpackList(data_.data() + currentBoxOffset(), false, slots, boxCapacity_);
        } else if (boxStorageValid(b)) {
            unpackList(data_.data() + boxListOffset(b), false, slots, boxCapacity_);
        }
        for (int i = 0; i < boxCapacity_; ++i) {
            if (slots[i].present()) fillPartyArea(slots[i].blob);
        }
    }
    Slot raw[kPartyMax];
    unpackList(data_.data() + partyListOffset(), true, raw, kPartyMax);
    partyCount_ = 0;
    for (int i = 0; i < kPartyMax; ++i) {
        party_[i] = Slot();
        if (raw[i].present()) party_[partyCount_++] = raw[i];
    }
}

void GbSave::packDirty() {
    const int current = currentBox();
    for (int b = 0; b < boxCount_ && !allBoxesDirty_; ++b) {
        if (boxDirty_[static_cast<size_t>(b)] && b != current && !boxStorageValid(b)) {
            if (initialiseBoxStorage()) allBoxesDirty_ = true;
            break;
        }
    }
    for (int b = 0; b < boxCount_; ++b) {
        if (!allBoxesDirty_ && !boxDirty_[static_cast<size_t>(b)]) continue;
        const Slot* slots = &boxes_[static_cast<size_t>(b) * static_cast<size_t>(boxCapacity_)];
        uint8_t* storage = data_.data() + boxListOffset(b);
        const bool initialised = boxStorageValid(b);
        const bool wrote = packList(storage, false, slots, initialised);
        if (b == current) {
            uint8_t* mirror = data_.data() + currentBoxOffset();
            if (wrote) {
                std::memcpy(mirror, storage, listLength(false));
            } else {
                packList(mirror, false, slots, initialised);
            }
        }
    }
    if (partyDirty_) packList(data_.data() + partyListOffset(), true, party_, true);
    std::fill(boxDirty_.begin(), boxDirty_.end(), false);
    allBoxesDirty_ = false;
    partyDirty_ = false;
}

void GbSave::refreshChecksums() {
    packDirty();
    writeChecksums();
}

SlotState GbSave::readPartySlot(int slot, uint8_t* out) const {
    if (slot < 0 || slot >= partyCount_) return SlotState::Empty;
    std::memcpy(out, party_[slot].blob, geometry_.blobSize);
    return SlotState::Valid;
}

bool GbSave::writePartySlot(int slot, const uint8_t* in) {
    if (slot < 0 || slot >= kPartyMax) return false;
    Slot& s = party_[slot];
    std::memcpy(s.blob, in, geometry_.blobSize);
    setMarker(s);
    partyDirty_ = true;
    return true;
}

void GbSave::setPartyCount(int count) {
    partyCount_ = std::clamp(count, 0, static_cast<int>(kPartyMax));
    partyDirty_ = true;
}

void GbSave::clearPartySlot(int slot) {
    if (slot < 0 || slot >= kPartyMax) return;
    party_[slot] = Slot();
    partyDirty_ = true;
}

SlotState GbSave::readBoxSlot(int box, int slot, uint8_t* out) const {
    if (box < 0 || box >= boxCount_ || slot < 0 || slot >= boxCapacity_) return SlotState::Empty;
    const Slot& s = boxes_[static_cast<size_t>(box) * static_cast<size_t>(boxCapacity_) + static_cast<size_t>(slot)];
    if (!s.present()) return SlotState::Empty;
    std::memcpy(out, s.blob, geometry_.blobSize);
    fillPartyArea(out);
    return SlotState::Valid;
}

bool GbSave::writeBoxSlot(int box, int slot, const uint8_t* in) {
    if (box < 0 || box >= boxCount_ || slot < 0 || slot >= boxCapacity_) return false;
    Slot& s = boxes_[static_cast<size_t>(box) * static_cast<size_t>(boxCapacity_) + static_cast<size_t>(slot)];
    std::memcpy(s.blob, in, geometry_.blobSize);
    setMarker(s);
    boxDirty_[static_cast<size_t>(box)] = true;
    return true;
}

bool GbSave::clearBoxSlot(int box, int slot) {
    if (box < 0 || box >= boxCount_ || slot < 0 || slot >= boxCapacity_) return false;
    boxes_[static_cast<size_t>(box) * static_cast<size_t>(boxCapacity_) + static_cast<size_t>(slot)] = Slot();
    boxDirty_[static_cast<size_t>(box)] = true;
    return true;
}

}  // namespace pktool::saves
