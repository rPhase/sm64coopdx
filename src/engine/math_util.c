#include <ultra64.h>

#include "sm64.h"
#include "engine/graph_node.h"
#include "math_util.h"
#include "surface_collision.h"

#include "trig_tables.inc.c"

inline f32 sins(s16 sm64Angle) {
    return gSineTable[(u16) (sm64Angle) >> 4];
}

inline f32 coss(s16 sm64Angle) {
    return gCosineTable[(u16) (sm64Angle) >> 4];
}

/**
 * Helper function for atan2s. Does a look up of the arctangent of y/x assuming
 * the resulting angle is in range [0, 0x2000] (1/8 of a circle).
 */
static OPTIMIZE_O3 u16 atan2_lookup(f32 y, f32 x) {
    s16 idx = (s16)(y / x * 1024.0f + 0.5f);
    idx = (idx >= 0 && idx < 0x401) ? idx : 0;
    return gArctanTable[idx];
}

/**
 * Compute the angle from (0, 0) to (x, y) as a s16. Given that terrain is in
 * the xz-plane, this is commonly called with (z, x) to get a yaw angle.
 */
inline s16 atan2s(f32 y, f32 x) {
    // Extract sign bits: 1 if negative, 0 otherwise
    u8 signx = (x < 0.0f);
    u8 signy = (y < 0.0f);

    // Take absolute values
    f32 absx = absx(x);
    f32 absy = absx(y);

    // Compute the angle in the first octant
    u16 angle = atan2_lookup(min(absx, absy), max(absy, absx));

    // Create an index based on the signs and swap status
    u8 idx = ((absy > absx) << 2) | (signx << 1) | signy;

    // Combined lookup tables for offsets and sign multipliers
    static const s16 offsets[] = {0x4000, 0x4000, 0xC000, 0xC000, 0x0000, 0x8000, 0x0000, 0x8000};
    static const s8 signs[] = {-1,  1,  1, -1, 1, -1, -1,  1};

    // Adjust output for (0, 0) edge case
    s16 zeroAdj = (x == 0.0f && y == 0.0f) * -0x4000;

    // Ensure the result fits into 16 bits via an explicit cast on angle
    return ((offsets[idx] + (signs[idx] * (s16)angle)) + zeroAdj) & 0xFFFF;
}

/**
 * Compute the atan2 in radians by calling atan2s and converting the result.
 */
f32 atan2f(f32 y, f32 x) {
    return (f32) atan2s(y, x) * M_PI / 0x8000;
}

/**
 * Return the value 'current' after it tries to approach target, going up at
 * most 'inc' and going down at most 'dec'.
 */
OPTIMIZE_O3 s32 approach_s32(s32 current, s32 target, s32 inc, s32 dec) {
    //! If target is close to the max or min s32, then it's possible to overflow
    // past it without stopping.

    if (current < target) {
        current += inc;
        if (current > target) {
            current = target;
        }
    } else {
        current -= dec;
        if (current < target) {
            current = target;
        }
    }
    return current;
}

/**
 * Return the value 'current' after it tries to approach target, going up at
 * most 'inc' and going down at most 'dec'.
 */
OPTIMIZE_O3 f32 approach_f32(f32 current, f32 target, f32 inc, f32 dec) {
    if (current < target) {
        current += inc;
        if (current > target) {
            current = target;
        }
    } else {
        current -= dec;
        if (current < target) {
            current = target;
        }
    }
    return current;
}

#define CURVE_BEGIN_1 1
#define CURVE_BEGIN_2 2
#define CURVE_MIDDLE 3
#define CURVE_END_1 4
#define CURVE_END_2 5

/**
 * Set 'result' to a 4-vector with weights corresponding to interpolation
 * value t in [0, 1] and gSplineState. Given the current control point P, these
 * weights are for P[0], P[1], P[2] and P[3] to obtain an interpolated point.
 * The weights naturally sum to 1, and they are also always in range [0, 1] so
 * the interpolated point will never overshoot. The curve is guaranteed to go
 * through the first and last point, but not through intermediate points.
 *
 * gSplineState ensures that the curve is clamped: the first two points
 * and last two points have different weight formulas. These are the weights
 * just before gSplineState transitions:
 * 1: [1, 0, 0, 0]
 * 1->2: [0, 3/12, 7/12, 2/12]
 * 2->3: [0, 1/6, 4/6, 1/6]
 * 3->3: [0, 1/6, 4/6, 1/6] (repeats)
 * 3->4: [0, 1/6, 4/6, 1/6]
 * 4->5: [0, 2/12, 7/12, 3/12]
 * 5: [0, 0, 0, 1]
 *
 * I suspect that the weight formulas will give a 3rd degree B-spline with the
 * common uniform clamped knot vector, e.g. for n points:
 * [0, 0, 0, 0, 1, 2, ... n-1, n, n, n, n]
 * TODO: verify the classification of the spline / figure out how polynomials were computed
 */
OPTIMIZE_O3 void spline_get_weights(struct MarioState* m, Vec4f result, f32 t, UNUSED s32 c) {
    if (!m) { return; }
    f32 tinv = 1 - t;
    f32 tinv2 = tinv * tinv;
    f32 tinv3 = tinv2 * tinv;
    f32 t2 = t * t;
    f32 t3 = t2 * t;

    switch (m->splineState) {
        case CURVE_BEGIN_1:
            result[0] = tinv3;
            result[1] = t3 * 1.75f - t2 * 4.5f + t * 3.0f;
            result[2] = -t3 * (11 / 12.0f) + t2 * 1.5f;
            result[3] = t3 * (1 / 6.0f);
            break;
        case CURVE_BEGIN_2:
            result[0] = tinv3 * 0.25f;
            result[1] = t3 * (7 / 12.0f) - t2 * 1.25f + t * 0.25f + (7 / 12.0f);
            result[2] = -t3 * 0.5f + t2 * 0.5f + t * 0.5f + (1 / 6.0f);
            result[3] = t3 * (1 / 6.0f);
            break;
        case CURVE_MIDDLE:
            result[0] = tinv3 * (1 / 6.0f);
            result[1] = t3 * 0.5f - t2 + (4 / 6.0f);
            result[2] = -t3 * 0.5f + t2 * 0.5f + t * 0.5f + (1 / 6.0f);
            result[3] = t3 * (1 / 6.0f);
            break;
        case CURVE_END_1:
            result[0] = tinv3 * (1 / 6.0f);
            result[1] = -tinv3 * 0.5f + tinv2 * 0.5f + tinv * 0.5f + (1 / 6.0f);
            result[2] = tinv3 * (7 / 12.0f) - tinv2 * 1.25f + tinv * 0.25f + (7 / 12.0f);
            result[3] = t3 * 0.25f;
            break;
        case CURVE_END_2:
            result[0] = tinv3 * (1 / 6.0f);
            result[1] = -tinv3 * (11 / 12.0f) + tinv2 * 1.5f;
            result[2] = tinv3 * 1.75f - tinv2 * 4.5f + tinv * 3.0f;
            result[3] = t3;
            break;
    }
}

/**
 * Initialize a spline animation.
 * 'keyFrames' should be an array of (s, x, y, z) vectors
 *  s: the speed of the keyframe in 1000/frames, e.g. s=100 means the keyframe lasts 10 frames
 *  (x, y, z): point in 3D space on the curve
 * The array should end with three entries with s=0 (infinite keyframe duration).
 * That's because the spline has a 3rd degree polynomial, so it looks 3 points ahead.
 */
OPTIMIZE_O3 void anim_spline_init(struct MarioState* m, Vec4s *keyFrames) {
    if (!m) { return; }
    m->splineKeyframe = keyFrames;
    m->splineKeyframeFraction = 0;
    m->splineState = 1;
}

/**
 * Poll the next point from a spline animation.
 * anim_spline_init should be called before polling for vectors.
 * Returns TRUE when the last point is reached, FALSE otherwise.
 */
OPTIMIZE_O3 s32 anim_spline_poll(struct MarioState* m, Vec3f result) {
    if (!m) { return 0; }
    Vec4f weights = { 0 };
    s32 i;
    s32 hasEnded = FALSE;

    vec3f_copy(result, gVec3fZero);
    spline_get_weights(m, weights, m->splineKeyframeFraction, m->splineState);

    if (m->splineKeyframe == NULL) { return FALSE; }

    for (i = 0; i < 4; i++) {
        result[0] += weights[i] * m->splineKeyframe[i][1];
        result[1] += weights[i] * m->splineKeyframe[i][2];
        result[2] += weights[i] * m->splineKeyframe[i][3];
    }

    if ((m->splineKeyframeFraction += m->splineKeyframe[0][0] / 1000.0f) >= 1) {
        m->splineKeyframe++;
        m->splineKeyframeFraction--;
        switch (m->splineState) {
            case CURVE_END_2:
                hasEnded = TRUE;
                break;
            case CURVE_MIDDLE:
                if (m->splineKeyframe[2][0] == 0) {
                    m->splineState = CURVE_END_1;
                }
                break;
            default:
                m->splineState++;
                break;
        }
    }

    return hasEnded;
}

  ///////////
 // Vec3f //
///////////

Vec3f gVec3fZero = { 0.0f, 0.0f, 0.0f };

Vec3f gVec3fOne = { 1.0f, 1.0f, 1.0f };

/**
 * Returns a vector rotated around the z axis, then the x axis, then the y
 * axis.
 */
OPTIMIZE_O3 Vec3fp vec3f_rotate_zxy(Vec3f dest, Vec3s rotate) {
    Vec3f v = { dest[0], dest[1], dest[2] };

    f32 sx = sins(rotate[0]);
    f32 cx = coss(rotate[0]);

    f32 sy = sins(rotate[1]);
    f32 cy = coss(rotate[1]);

    f32 sz = sins(rotate[2]);
    f32 cz = coss(rotate[2]);

    f32 sysz = (sy * sz);
    f32 cycz = (cy * cz);
    f32 cysz = (cy * sz);
    f32 sycz = (sy * cz);

    dest[0] = v[0] * ((sysz * sx) + cycz) + v[1] * ((sycz * sx) - cysz) + v[2] * (cx * sy);
    dest[1] = v[0] * (cx * sz) + v[1] * (cx * cz) + v[2] * -sx;
    dest[2] = v[0] * ((cysz * sx) - sycz) + v[1] * ((cycz * sx) + sysz) + v[2] * (cx * cy);
    return dest;
}

// Rodrigues' formula
// dest = v * cos(r) + (n x v) * sin(r) + n * (n . v) * (1 - cos(r))
OPTIMIZE_O3 Vec3fp vec3f_rotate_around_n(Vec3f dest, Vec3f v, Vec3f n, s16 r) {
    Vec3f nvCross;
    vec3f_cross(nvCross, n, v);
    f32 nvDot = vec3f_dot(n, v);
    f32 cosr = coss(r);
    f32 sinr = sins(r);
    dest[0] = v[0] * cosr + nvCross[0] * sinr + n[0] * nvDot * (1.f - cosr);
    dest[1] = v[1] * cosr + nvCross[1] * sinr + n[1] * nvDot * (1.f - cosr);
    dest[2] = v[2] * cosr + nvCross[2] * sinr + n[2] * nvDot * (1.f - cosr);
    return dest;
}

OPTIMIZE_O3 Vec3fp vec3f_project(Vec3f dest, Vec3f v, Vec3f onto) {
    f32 numerator = vec3f_dot(v, onto);
    f32 denominator = vec3f_dot(onto, onto);
    if (denominator == 0) {
        return vec3f_zero(dest);
    }

    vec3f_copy(dest, onto);
    vec3f_mul(dest, numerator / denominator);
    return dest;
}

OPTIMIZE_O3 Vec3fp vec3f_transform(Vec3f dest, Vec3f v, Vec3f translation, Vec3s rotation, Vec3f scale) {
    vec3f_copy(dest, v);

    // scale
    dest[0] *= scale[0];
    dest[1] *= scale[1];
    dest[2] *= scale[2];

    // rotation
    vec3f_rotate_zxy(dest, rotation);

    // translation
    vec3f_add(dest, translation);

    return dest;
}

/**
 * Take the vector starting at 'from' pointed at 'to' an retrieve the length
 * of that vector, as well as the yaw and pitch angles.
 * Basically it converts the direction to spherical coordinates.
 */
OPTIMIZE_O3 void vec3f_get_dist_and_angle(Vec3f from, Vec3f to, f32 *dist, s16 *pitch, s16 *yaw) {
    f32 x = to[0] - from[0];
    f32 y = to[1] - from[1];
    f32 z = to[2] - from[2];

    *dist = sqrtf(x * x + y * y + z * z);
    *pitch = atan2s(sqrtf(x * x + z * z), y);
    *yaw = atan2s(z, x);
}

/**
 * Construct the 'to' point which is distance 'dist' away from the 'from' position,
 * and has the angles pitch and yaw.
 */
OPTIMIZE_O3 void vec3f_set_dist_and_angle(Vec3f from, Vec3f to, f32 dist, s16 pitch, s16 yaw) {
    to[0] = from[0] + dist * coss(pitch) * sins(yaw);
    to[1] = from[1] + dist * sins(pitch);
    to[2] = from[2] + dist * coss(pitch) * coss(yaw);
}

/**
 * Set 'dest' the normal vector of a triangle with vertices a, b and c.
 * It is similar to vec3f_cross, but it calculates the vectors (c-b) and (b-a)
 * at the same time.
 */
OPTIMIZE_O3 Vec3fp find_vector_perpendicular_to_plane(Vec3f dest, Vec3f a, Vec3f b, Vec3f c) {
    dest[0] = (b[1] - a[1]) * (c[2] - b[2]) - (c[1] - b[1]) * (b[2] - a[2]);
    dest[1] = (b[2] - a[2]) * (c[0] - b[0]) - (c[2] - b[2]) * (b[0] - a[0]);
    dest[2] = (b[0] - a[0]) * (c[1] - b[1]) - (c[0] - b[0]) * (b[1] - a[1]);
    return dest;
}

  ///////////
 // Vec3i //
///////////

Vec3i gVec3iZero = { 0, 0, 0 };

Vec3i gVec3iOne = { 1, 1, 1 };

  ///////////
 // Vec3s //
///////////

Vec3s gVec3sZero = { 0, 0, 0 };

Vec3s gVec3sOne = { 1, 1, 1 };

  //////////
 // Mat4 //
//////////

Mat4 gMat4Identity = {
    { 1, 0, 0, 0 },
    { 0, 1, 0, 0 },
    { 0, 0, 1, 0 },
    { 0, 0, 0, 1 },
};

Mat4 gMat4Zero = {
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
};

/**
 * Set mtx to a look-at matrix for the camera. The resulting transformation
 * transforms the world as if there exists a camera at position 'from' pointed
 * at the position 'to'. The up-vector is assumed to be (0, 1, 0), but the 'roll'
 * angle allows a bank rotation of the camera.
 */
OPTIMIZE_O3 void mtxf_lookat(Mat4 mtx, Vec3f from, Vec3f to, s16 roll) {
    Vec3f forward, right, up;
    f32 sinRoll, cosRoll;
    f32 dx, dz, xzDist;
    f32 invLength;

    forward[0] = from[0] - to[0];
    forward[1] = from[1] - to[1];
    forward[2] = from[2] - to[2];
    invLength = 1.0f / sqrtf(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
    forward[0] *= invLength;
    forward[1] *= invLength;
    forward[2] *= invLength;

    dx = to[0] - from[0];
    dz = to[2] - from[2];
    xzDist = dx * dx + dz * dz;
    if (xzDist != 0.0f) {
        invLength = -1.0f / sqrtf(xzDist);
        dx *= invLength;
        dz *= invLength;
    } else {
        dx = dz = 0.0f;
    }

    sinRoll = sins(roll);
    cosRoll = coss(roll);

    up[0] = sinRoll * dz;
    up[1] = cosRoll;
    up[2] = -sinRoll * dx;

    right[0] = up[1] * forward[2] - up[2] * forward[1];
    right[1] = up[2] * forward[0] - up[0] * forward[2];
    right[2] = up[0] * forward[1] - up[1] * forward[0];

    invLength = 1.0f / sqrtf(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    right[0] *= invLength;
    right[1] *= invLength;
    right[2] *= invLength;

    up[0] = forward[1] * right[2] - forward[2] * right[1];
    up[1] = forward[2] * right[0] - forward[0] * right[2];
    up[2] = forward[0] * right[1] - forward[1] * right[0];

    mtx[0][0] = right[0];
    mtx[1][0] = right[1];
    mtx[2][0] = right[2];
    mtx[3][0] = -(from[0] * right[0] + from[1] * right[1] + from[2] * right[2]);

    mtx[0][1] = up[0];
    mtx[1][1] = up[1];
    mtx[2][1] = up[2];
    mtx[3][1] = -(from[0] * up[0] + from[1] * up[1] + from[2] * up[2]);

    mtx[0][2] = forward[0];
    mtx[1][2] = forward[1];
    mtx[2][2] = forward[2];
    mtx[3][2] = -(from[0] * forward[0] + from[1] * forward[1] + from[2] * forward[2]);

    mtx[0][3] = mtx[1][3] = mtx[2][3] = 0.0f;
    mtx[3][3] = 1.0f;
}

/**
 * Build a matrix that rotates around the z axis, then the x axis, then the y
 * axis, and then translates.
 */
OPTIMIZE_O3 void mtxf_rotate_zxy_and_translate(Mat4 dest, Vec3f translate, Vec3s rotate) {
    f32 sx = sins(rotate[0]);
    f32 cx = coss(rotate[0]);

    f32 sy = sins(rotate[1]);
    f32 cy = coss(rotate[1]);

    f32 sz = sins(rotate[2]);
    f32 cz = coss(rotate[2]);

    dest[0][0] = cy * cz + sx * sy * sz;
    dest[1][0] = -cy * sz + sx * sy * cz;
    dest[2][0] = cx * sy;
    dest[3][0] = translate[0];

    dest[0][1] = cx * sz;
    dest[1][1] = cx * cz;
    dest[2][1] = -sx;
    dest[3][1] = translate[1];

    dest[0][2] = -sy * cz + sx * cy * sz;
    dest[1][2] = sy * sz + sx * cy * cz;
    dest[2][2] = cx * cy;
    dest[3][2] = translate[2];

    dest[0][3] = dest[1][3] = dest[2][3] = 0.0f;
    dest[3][3] = 1.0f;
}

/**
 * Build a matrix that rotates around the x axis, then the y axis, then the z
 * axis, and then translates.
 */
OPTIMIZE_O3 void mtxf_rotate_xyz_and_translate(Mat4 dest, Vec3f b, Vec3s c) {
    f32 sx = sins(c[0]);
    f32 cx = coss(c[0]);

    f32 sy = sins(c[1]);
    f32 cy = coss(c[1]);

    f32 sz = sins(c[2]);
    f32 cz = coss(c[2]);

    dest[0][0] = cy * cz;
    dest[0][1] = cy * sz;
    dest[0][2] = -sy;
    dest[0][3] = 0;

    dest[1][0] = sx * sy * cz - cx * sz;
    dest[1][1] = sx * sy * sz + cx * cz;
    dest[1][2] = sx * cy;
    dest[1][3] = 0;

    dest[2][0] = cx * sy * cz + sx * sz;
    dest[2][1] = cx * sy * sz - sx * cz;
    dest[2][2] = cx * cy;
    dest[2][3] = 0;

    dest[3][0] = b[0];
    dest[3][1] = b[1];
    dest[3][2] = b[2];
    dest[3][3] = 1;
}

/**
 * Set 'dest' to a transformation matrix that turns an object to face the camera.
 * 'mtx' is the look-at matrix from the camera
 * 'position' is the position of the object in the world
 * 'angle' rotates the object while still facing the camera.
 */
OPTIMIZE_O3 void mtxf_billboard(Mat4 dest, Mat4 mtx, Vec3f position, s16 angle) {
    dest[0][0] = coss(angle);
    dest[0][1] = sins(angle);
    dest[0][2] = 0;
    dest[0][3] = 0;

    dest[1][0] = -dest[0][1];
    dest[1][1] = dest[0][0];
    dest[1][2] = 0;
    dest[1][3] = 0;

    dest[2][0] = 0;
    dest[2][1] = 0;
    dest[2][2] = 1;
    dest[2][3] = 0;

    dest[3][0] = mtx[0][0] * position[0] + mtx[1][0] * position[1] + mtx[2][0] * position[2] + mtx[3][0];
    dest[3][1] = mtx[0][1] * position[0] + mtx[1][1] * position[1] + mtx[2][1] * position[2] + mtx[3][1];
    dest[3][2] = mtx[0][2] * position[0] + mtx[1][2] * position[1] + mtx[2][2] * position[2] + mtx[3][2];
    dest[3][3] = 1;
}

// straight up mtxf_billboard but minus the dest[1][n] lines. transform for cylindrical billboards
OPTIMIZE_O3 void mtxf_cylboard(Mat4 dest, Mat4 mtx, Vec3f position, s16 angle) {
    dest[0][0] = coss(angle);
    dest[0][1] = sins(angle);
    dest[0][2] = 0;
    dest[0][3] = 0;

    dest[1][0] = mtx[1][0];
    dest[1][1] = mtx[1][1];
    dest[1][2] = mtx[1][2];
    dest[1][3] = 0;

    dest[2][0] = 0;
    dest[2][1] = 0;
    dest[2][2] = 1;
    dest[2][3] = 0;

    dest[3][0] = mtx[0][0] * position[0] + mtx[1][0] * position[1] + mtx[2][0] * position[2] + mtx[3][0];
    dest[3][1] = mtx[0][1] * position[0] + mtx[1][1] * position[1] + mtx[2][1] * position[2] + mtx[3][1];
    dest[3][2] = mtx[0][2] * position[0] + mtx[1][2] * position[1] + mtx[2][2] * position[2] + mtx[3][2];
    dest[3][3] = 1;
}

/**
 * Set 'dest' to a transformation matrix that aligns an object with the terrain
 * based on the normal. Used for enemies.
 * 'upDir' is the terrain normal
 * 'yaw' is the angle which it should face
 * 'pos' is the object's position in the world
 */
OPTIMIZE_O3 void mtxf_align_terrain_normal(Mat4 dest, Vec3f upDir, Vec3f pos, s16 yaw) {
    Vec3f lateralDir;
    Vec3f leftDir;
    Vec3f forwardDir;

    vec3f_set(lateralDir, sins(yaw), 0, coss(yaw));
    vec3f_normalize(upDir);

    vec3f_cross(leftDir, upDir, lateralDir);
    vec3f_normalize(leftDir);

    vec3f_cross(forwardDir, leftDir, upDir);
    vec3f_normalize(forwardDir);

    dest[0][0] = leftDir[0];
    dest[0][1] = leftDir[1];
    dest[0][2] = leftDir[2];
    dest[3][0] = pos[0];

    dest[1][0] = upDir[0];
    dest[1][1] = upDir[1];
    dest[1][2] = upDir[2];
    dest[3][1] = pos[1];

    dest[2][0] = forwardDir[0];
    dest[2][1] = forwardDir[1];
    dest[2][2] = forwardDir[2];
    dest[3][2] = pos[2];

    dest[0][3] = 0.0f;
    dest[1][3] = 0.0f;
    dest[2][3] = 0.0f;
    dest[3][3] = 1.0f;
}

/**
 * Set 'mtx' to a transformation matrix that aligns an object with the terrain
 * based on 3 height samples in an equilateral triangle around the object.
 * Used for Mario when crawling or sliding.
 * 'yaw' is the angle which it should face
 * 'pos' is the object's position in the world
 * 'radius' is the distance from each triangle vertex to the center
 */
OPTIMIZE_O3 void mtxf_align_terrain_triangle(Mat4 mtx, Vec3f pos, s16 yaw, f32 radius) {
    struct Surface *sp74;
    Vec3f point0;
    Vec3f point1;
    Vec3f point2;
    Vec3f forward;
    Vec3f xColumn;
    Vec3f yColumn;
    Vec3f zColumn;
    f32 avgY;
    f32 minY = -radius * 3;

    point0[0] = pos[0] + radius * sins(yaw + 0x2AAA);
    point0[2] = pos[2] + radius * coss(yaw + 0x2AAA);
    point1[0] = pos[0] + radius * sins(yaw + 0x8000);
    point1[2] = pos[2] + radius * coss(yaw + 0x8000);
    point2[0] = pos[0] + radius * sins(yaw + 0xD555);
    point2[2] = pos[2] + radius * coss(yaw + 0xD555);

    point0[1] = find_floor(point0[0], pos[1] + 150, point0[2], &sp74);
    point1[1] = find_floor(point1[0], pos[1] + 150, point1[2], &sp74);
    point2[1] = find_floor(point2[0], pos[1] + 150, point2[2], &sp74);

    if (point0[1] - pos[1] < minY) {
        point0[1] = pos[1];
    }

    if (point1[1] - pos[1] < minY) {
        point1[1] = pos[1];
    }

    if (point2[1] - pos[1] < minY) {
        point2[1] = pos[1];
    }

    avgY = (point0[1] + point1[1] + point2[1]) / 3;

    vec3f_set(forward, sins(yaw), 0, coss(yaw));
    find_vector_perpendicular_to_plane(yColumn, point0, point1, point2);
    vec3f_normalize(yColumn);
    vec3f_cross(xColumn, yColumn, forward);
    vec3f_normalize(xColumn);
    vec3f_cross(zColumn, xColumn, yColumn);
    vec3f_normalize(zColumn);

    mtx[0][0] = xColumn[0];
    mtx[0][1] = xColumn[1];
    mtx[0][2] = xColumn[2];
    mtx[3][0] = pos[0];

    mtx[1][0] = yColumn[0];
    mtx[1][1] = yColumn[1];
    mtx[1][2] = yColumn[2];
    mtx[3][1] = (avgY < pos[1]) ? pos[1] : avgY;

    mtx[2][0] = zColumn[0];
    mtx[2][1] = zColumn[1];
    mtx[2][2] = zColumn[2];
    mtx[3][2] = pos[2];

    mtx[0][3] = 0;
    mtx[1][3] = 0;
    mtx[2][3] = 0;
    mtx[3][3] = 1;
}

/**
 * Sets matrix 'dest' to the matrix product b * a.
 * The resulting matrix represents first applying transformation b and
 * then a.
 */
OPTIMIZE_O3 void mtxf_mul(Mat4 dest, Mat4 a, Mat4 b) {
    Mat4 tmp;
    for (s32 i = 0; i < 4; i++) {
        for (s32 j = 0; j < 4; j++) {
            tmp[i][j] = a[i][0] * b[0][j] +
                        a[i][1] * b[1][j] +
                        a[i][2] * b[2][j] +
                        a[i][3] * b[3][j];
        }
    }
    mtxf_copy(dest, tmp);
}

/**
 * Multiply a vector with a transformation matrix, which applies the transformation
 * to the point. Note that the bottom row is assumed to be [0, 0, 0, 1], which is
 * true for transformation matrices if the translation has a w component of 1.
 */
OPTIMIZE_O3 s16 *mtxf_mul_vec3s(Mat4 mtx, Vec3s b) {
    f32 x = b[0];
    f32 y = b[1];
    f32 z = b[2];

    b[0] = x * mtx[0][0] + y * mtx[1][0] + z * mtx[2][0] + mtx[3][0];
    b[1] = x * mtx[0][1] + y * mtx[1][1] + z * mtx[2][1] + mtx[3][1];
    b[2] = x * mtx[0][2] + y * mtx[1][2] + z * mtx[2][2] + mtx[3][2];
    
    return b;
}

/**
 * Set 'mtx' to a transformation matrix that rotates around the z axis.
 */
OPTIMIZE_O3 void mtxf_rotate_xy(Mat4 mtx, s16 angle) {
    mtxf_identity(mtx);
    mtx[0][0] = coss(angle);
    mtx[0][1] = sins(angle);
    mtx[1][0] = -mtx[0][1];
    mtx[1][1] = mtx[0][0];
}

/**
 * Get inverse matrix 'dest' of matrix 'src'.
 *
 * fast inverse matrix code is brought over from "inverse.c" from Graphics Gems II
 * Author: Kevin Wu
 * additional Graphics Gems code by Andrew Glassner and Rod G. Bogart
 * http://www.realtimerendering.com/resources/GraphicsGems/gemsii/inverse.c
 *
 * this function assumes the transform is affine
 * matrix perspective is not used in SM64, so this isn't a concern
 * furthermore, this is currently only used to get the inverse of the camera transform
 * because that is always orthonormal, the determinant will never be 0, so that check is removed
 */
OPTIMIZE_O3 void mtxf_inverse(Mat4 dest, Mat4 src) {
    Mat4 buf;

    // calculating the determinant has been reduced since the check is removed
    f32 det_1 = 1.0f / (
          src[0][0] * src[1][1] * src[2][2]
        + src[0][1] * src[1][2] * src[2][0]
        + src[0][2] * src[1][0] * src[2][1]
        - src[0][2] * src[1][1] * src[2][0]
        - src[0][1] * src[1][0] * src[2][2]
        - src[0][0] * src[1][2] * src[2][1]
    );

    // inverse of axis vectors (adj(A) / det(A))
    buf[0][0] = (src[1][1] * src[2][2] - src[1][2] * src[2][1]) * det_1;
    buf[1][0] = (src[1][2] * src[2][0] - src[1][0] * src[2][2]) * det_1;
    buf[2][0] = (src[1][0] * src[2][1] - src[1][1] * src[2][0]) * det_1;
    buf[0][1] = (src[0][2] * src[2][1] - src[0][1] * src[2][2]) * det_1;
    buf[1][1] = (src[0][0] * src[2][2] - src[0][2] * src[2][0]) * det_1;
    buf[2][1] = (src[0][1] * src[2][0] - src[0][0] * src[2][1]) * det_1;
    buf[0][2] = (src[0][1] * src[1][2] - src[0][2] * src[1][1]) * det_1;
    buf[1][2] = (src[0][2] * src[1][0] - src[0][0] * src[1][2]) * det_1;
    buf[2][2] = (src[0][0] * src[1][1] - src[0][1] * src[1][0]) * det_1;

    // inverse of translation (-C * inv(A))
    buf[3][0] = -src[3][0] * buf[0][0] - src[3][1] * buf[1][0] - src[3][2] * buf[2][0];
    buf[3][1] = -src[3][0] * buf[0][1] - src[3][1] * buf[1][1] - src[3][2] * buf[2][1];
    buf[3][2] = -src[3][0] * buf[0][2] - src[3][1] * buf[1][2] - src[3][2] * buf[2][2];

    buf[0][3] = buf[1][3] = buf[2][3] = 0.0f;
    buf[3][3] = 1.0f;

    mtxf_copy(dest, buf);
}

/**
 * Extract a position given an object's transformation matrix and a camera matrix.
 * This is used for determining the world position of the held object: since objMtx
 * inherits the transformation from both the camera and Mario, it calculates this
 * by taking the camera matrix and inverting its transformation by first rotating
 * objMtx back from screen orientation to world orientation, and then subtracting
 * the camera position.
 */
OPTIMIZE_O3 Vec3fp get_pos_from_transform_mtx(Vec3f dest, Mat4 objMtx, Mat4 camMtx) {
    f32 camX = camMtx[3][0] * camMtx[0][0] + camMtx[3][1] * camMtx[0][1] + camMtx[3][2] * camMtx[0][2];
    f32 camY = camMtx[3][0] * camMtx[1][0] + camMtx[3][1] * camMtx[1][1] + camMtx[3][2] * camMtx[1][2];
    f32 camZ = camMtx[3][0] * camMtx[2][0] + camMtx[3][1] * camMtx[2][1] + camMtx[3][2] * camMtx[2][2];

    dest[0] = objMtx[3][0] * camMtx[0][0] + objMtx[3][1] * camMtx[0][1] + objMtx[3][2] * camMtx[0][2] - camX;
    dest[1] = objMtx[3][0] * camMtx[1][0] + objMtx[3][1] * camMtx[1][1] + objMtx[3][2] * camMtx[1][2] - camY;
    dest[2] = objMtx[3][0] * camMtx[2][0] + objMtx[3][1] * camMtx[2][1] + objMtx[3][2] * camMtx[2][2] - camZ;
        
    return dest;
}
