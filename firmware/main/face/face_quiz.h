#ifndef NB3_FACE_QUIZ_H
#define NB3_FACE_QUIZ_H

#include "behavior/expression_arbiter.h"
#include "face/face_display.h"

// Bench reading test of the faces (docs/PLANO-FACE-E-COMPORTAMENTO, section 10),
// only in a build with NB3_FACE_QUIZ: three passes over every face in random
// order, each shown with no caption after a numbered gap. The key (item number
// and face) goes to the log, never to the screen. In a normal build Start()
// does nothing.
class FaceQuiz {
public:
    FaceQuiz(ExpressionArbiter& arbiter, FaceDisplay* display);

    FaceQuiz(const FaceQuiz&) = delete;
    FaceQuiz& operator=(const FaceQuiz&) = delete;

    void Start();

private:
    static void TaskEntry(void* arg);
    void Run();

    ExpressionArbiter& arbiter_;
    FaceDisplay* display_;
};

#endif  // NB3_FACE_QUIZ_H
