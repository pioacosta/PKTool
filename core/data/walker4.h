#pragma once

#include <cstdint>

// HG/SS Pokewalker courses, generated from PKHeX (encounter_walker4.pkl).
//
// Every course offers 6 species in 3 groups of 2 (groups A, B and C); the
// Pokemon caught on a stroll have the slot's fixed level, gender and moves.
namespace pktool::data {

constexpr int kWalkerCourses = 27;
constexpr int kWalkerSlotsPerCourse = 6;

struct WalkerSlot {
    uint16_t species;
    uint8_t level;
    uint8_t gender;  // 0 male, 1 female, 2 genderless
    uint16_t moves[4];
};

// The 6 slots of a course (0 <= course < kWalkerCourses).
const WalkerSlot* walkerSlots(int course);
const char* walkerCourseName(int course);

namespace generated {
extern const WalkerSlot kWalkerSlots[];
extern const char* const kWalkerCourseNames[];
}  // namespace generated

}  // namespace pktool::data
