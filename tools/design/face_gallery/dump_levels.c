// Prints the six family generators at three intensities each (the Study 02
// matrix: subtle, expressive, peak) as JSON, one row per family, one frame per
// level: [t, blink, dizzy, faceScale, faceX, faceY, leftEye, rightEye]. It
// includes nb_avatar.c to reach the generators, which are not public.
#include <stdio.h>

#include "nb_avatar.c"

// Joy's peak is the loving heart: its shape is the recipe's own.
#define JOY_PEAK (-1.0f)

static const struct {
    const char *name;
    nb_avatar_family_t family;
    float levels[3];
} kRows[] = {
    {"PRESENCA", FAMILY_PRESENCE, {0.0f, 0.4f, 1.0f}},
    {"ALEGRIA", FAMILY_JOY, {0.12f, 0.55f, JOY_PEAK}},
    {"CURIOSIDADE", FAMILY_CURIOSITY, {0.25f, 0.6f, 1.0f}},
    {"TRISTEZA", FAMILY_SADNESS, {0.1f, 0.5f, 1.0f}},
    {"CONTRARIEDADE", FAMILY_CONTRARY, {0.1f, 0.5f, 1.0f}},
    {"SONO", FAMILY_SLEEPINESS, {0.15f, 0.6f, 0.6f}},
};

static void print_eye(const nb_avatar_eye_pose_t *e) {
    printf("[%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f]", e->x, e->y, e->width,
           e->height, e->roundness, e->upper_lid, e->upper_lid_tilt, e->lower_lid, e->crescent,
           e->angle, e->heart);
}

int main(void) {
    printf("{\"rows\":[");
    for (size_t r = 0; r < sizeof(kRows) / sizeof(kRows[0]); ++r) {
        printf("%s{\"name\":\"%s\",\"levels\":[", r ? "," : "", kRows[r].name);
        for (int l = 0; l < 3; ++l) {
            nb_avatar_shape_t shape = {.a = kRows[r].levels[l]};
            if (kRows[r].levels[l] == JOY_PEAK) {
                shape = s_recipes[NB_AVATAR_EXPR_LOVING].peak;
            }
            nb_avatar_pose_t pose = generate(kRows[r].family, &shape);
            // The last sleep level is the closed eye (the sleep), not a lid.
            if (kRows[r].family == FAMILY_SLEEPINESS && l == 2) {
                pose.left_eye.crescent = pose.right_eye.crescent = NB_AVATAR_SHUT;
                pose.left_eye.upper_lid = pose.right_eye.upper_lid = 0.0f;
            }
            // The head's roll tilts the eyes (the avatar core does this when projecting).
            pose.left_eye.angle += pose.head_roll;
            pose.right_eye.angle += pose.head_roll;
            printf("%s[0,1,0,%.3f,%.2f,%.2f,", l ? "," : "", pose.face_scale, pose.face_x,
                   pose.face_y);
            print_eye(&pose.left_eye);
            printf(",");
            print_eye(&pose.right_eye);
            printf("]");
        }
        printf("]}");
    }
    printf("]}\n");
    return 0;
}
