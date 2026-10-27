#ifndef NB3_POWER_CUT_TEST_H
#define NB3_POWER_CUT_TEST_H

#include <cstdint>

#include "face/face_display.h"
#include "memory/store_service.h"

// Bench test of VIDA.md §5.2, only in a build with NB3_POWER_CUT_TEST: a
// memory of its own, in files of its own (the robot's memory is not
// touched), written nonstop with episodes, remembered facts, forgets and
// compactions, so power can be cut in the middle of any write. Every boot
// loads it and checks that nothing is torn, no forget is left half done,
// the last confirmed fact is there and the last confirmed forget is gone;
// the result goes to the log and the banner. In a normal build Start() does
// nothing.
class PowerCutTest {
public:
    PowerCutTest(StoreService& store, FaceDisplay* display);

    PowerCutTest(const PowerCutTest&) = delete;
    PowerCutTest& operator=(const PowerCutTest&) = delete;

    void Start();

private:
    static void TaskEntry(void* arg);
    void Run();

    StoreService& store_;
    FaceDisplay* display_;
};

#endif  // NB3_POWER_CUT_TEST_H
