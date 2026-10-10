#include "core/legality/shadow_locks.h"

#include "core/legality/rng3.h"

namespace pktool::legality::shadow {
namespace {

constexpr int kMaxFrames = 1536;      // frames of the backwards RNG cache
constexpr int kMaxSteps = 400000;     // search budget (DS: a few ms per ten thousand)
constexpr int kMaxLocks = 8;

bool isShadow(const data::NpcLock3& l) { return l.state != 0; }
int framesConsumed(const data::NpcLock3& l) { return l.state > 1 ? 5 : 7; }

bool matchesLock(const data::NpcLock3& l, uint32_t pid) {
    if (isShadow(l) && l.nature == 0) return true;  // a shadow member without a lock
    if (l.gender != 2 && l.gender != ((pid & 0xFF) < l.ratio ? 1u : 0u)) return false;
    return l.nature == pid % 25;
}

class Search {
public:
    Search(const data::NpcLock3* locks, int count, uint32_t origin, uint32_t tsv)
        : locks_(locks), count_(count), tsv_(tsv) {
        seed_ = rng3::xdPrev(rng3::xdPrev(origin));
        vals_[0] = seed_ >> 16;
        size_ = 1;
    }

    bool run() { return count_ <= kMaxLocks && find(0, nullptr, count_); }

private:
    uint32_t at(int index) {
        if (index >= kMaxFrames) {
            aborted_ = true;
            return 0;
        }
        while (index >= size_) {
            seed_ = rng3::xdPrev(seed_);
            vals_[size_++] = seed_ >> 16;
        }
        return vals_[index];
    }

    bool tick() {
        if (++steps_ > kMaxSteps) aborted_ = true;
        return aborted_;
    }

    uint32_t pidAt(int frame) { return (at(frame + 1) << 16) | at(frame); }

    // Depth-first search; `remaining` locks are still to be placed (the last
    // member of the party is placed first, as the search runs backwards).
    bool find(int frame, const data::NpcLock3* prior, int remaining) {
        if (aborted_) return false;
        if (remaining == 0) return verify(frame);
        const data::NpcLock3& current = locks_[remaining - 1];
        if (prior == nullptr || isShadow(*prior)) return single(frame, current, remaining);
        return all(frame, current, *prior, remaining);
    }

    // The member must match precisely at this frame (rerolls shift it further).
    bool single(int ctr, const data::NpcLock3& current, int remaining) {
        uint32_t pid = pidAt(ctr);
        if (!matchesLock(current, pid)) return false;
        team_[remaining - 1] = pid;
        if (find(ctr + framesConsumed(current), &current, remaining - 1)) return true;

        // An anti-shiny reroll of the frame before it?
        bool forcedOt = false;
        int start = 2;
        while (!tick()) {
            uint32_t upper = at(start + 1);
            uint32_t lower = at(start);
            uint32_t sv = (upper ^ lower) >> 3;
            if (sv == tsv_) {
                // rerolled because it was shiny for the player: a possible frame
            } else if (rcsv_ != kNoTsv) {
                if (sv == rcsv_) {
                    forcedOt = true;
                    start += 2;  // PKHeX loops here without advancing; the budget cuts that off
                    continue;
                }
                if (forcedOt) rcsv_ = kNoTsv;
                return false;
            }
            team_[remaining - 1] = pid;
            if (find(start + framesConsumed(current), &current, remaining - 1)) return true;
            start += 2;
        }
        return false;
    }

    // The party member before a plain member: generation stops at an
    // "interrupt" frame where that plain member would have been produced.
    bool all(int ctr, const data::NpcLock3& current, const data::NpcLock3& prior, int remaining) {
        const int start = ctr;
        bool forcedOt = false;
        while (!tick()) {
            int p7 = ctr - 7;
            if (p7 > start) {
                uint32_t upper = at(p7 + 1);
                uint32_t lower = at(p7);
                uint32_t cid = (upper << 16) | lower;
                uint32_t sv = (upper ^ lower) >> 3;
                if (sv == tsv_) {
                    // shiny: this interrupt is ignored
                } else if (matchesLock(prior, cid)) {
                    if (rcsv_ != kNoTsv) {
                        if (sv != rcsv_) {
                            if (forcedOt) rcsv_ = kNoTsv;
                            return false;
                        }
                    } else {
                        rcsv_ = sv;
                        forcedOt = true;
                    }
                }
            }
            uint32_t pid = pidAt(ctr);
            if (matchesLock(current, pid)) {
                team_[remaining - 1] = pid;
                if (find(ctr + framesConsumed(current), &current, remaining - 1)) return true;
            }
            ctr += 2;
        }
        return false;
    }

    // The party has to be compatible with the CPU trainer's own IDs: nobody
    // but a shadow may be shiny for it.
    bool verify(int ctr) {
        uint32_t cpuSv = (at(ctr + 1) ^ at(ctr)) >> 3;
        if (aborted_) return false;
        if (rcsv_ != kNoTsv && rcsv_ != cpuSv) return false;
        for (int j = 0; j < count_; ++j) {
            uint32_t pid = team_[j];
            uint32_t psv = ((pid & 0xFFFF) ^ (pid >> 16)) >> 3;
            if (psv != cpuSv) continue;
            // PKHeX pairs member j with lock count-1-j here.
            if (!isShadow(locks_[count_ - 1 - j])) return false;
            if (tsv_ != kNoTsv) return false;
        }
        return true;
    }

    const data::NpcLock3* locks_;
    int count_;
    uint32_t tsv_;
    uint32_t rcsv_ = kNoTsv;
    uint32_t seed_ = 0;
    uint32_t vals_[kMaxFrames];
    int size_ = 0;
    uint32_t team_[kMaxLocks] = {};
    int steps_ = 0;
    bool aborted_ = false;
};

}  // namespace

bool locksSatisfied(const data::LockSet3& set, uint32_t origin, uint32_t tsv) {
    if (set.teamCount == 0) return true;
    for (int t = 0; t < set.teamCount; ++t) {
        const data::TeamLock3& team = data::teamLock3(set.firstTeam + t);
        Search search(&data::npcLock3(team.firstLock), team.lockCount, origin, tsv);
        if (search.run()) return true;
    }
    return false;
}

}  // namespace pktool::legality::shadow
