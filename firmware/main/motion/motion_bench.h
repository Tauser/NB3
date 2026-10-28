#ifndef NB3_MOTION_BENCH_H
#define NB3_MOTION_BENCH_H

#include "motion/motion_service.h"

// Bench test of the first motion, only in a build with NB3_MOTION_BENCH:
// after a pause it asks for a small look around inside the safety limits
// (center, left, right, up, down, center, 0.8 s each move) and holds for
// 20 s, over and over, so the safety can be seen cutting when a horn is held
// by hand; after a stop it waits for self.head.recover. A normal build asks
// for nothing and the head stays free.
void StartMotionBench(MotionService& motion);

#endif  // NB3_MOTION_BENCH_H
