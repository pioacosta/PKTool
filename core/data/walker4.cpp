#include "core/data/walker4.h"

namespace pktool::data {

const WalkerSlot* walkerSlots(int course) {
    if (course < 0 || course >= kWalkerCourses) return nullptr;
    return generated::kWalkerSlots + course * kWalkerSlotsPerCourse;
}

const char* walkerCourseName(int course) {
    if (course < 0 || course >= kWalkerCourses) return "?";
    return generated::kWalkerCourseNames[course];
}

}  // namespace pktool::data
