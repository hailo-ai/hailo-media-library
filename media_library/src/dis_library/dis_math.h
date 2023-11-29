/*
 * Copyright (c) 2017-2023 Hailo Technologies Ltd. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
/**
 * @file dis_math.h
 * @brief Mathematical operations and algebra for 2x2 and 3x3 matrices used by
 *the digital image stabilization library.
 **/
#ifndef _DIS_DIS_MATH_H_
#define _DIS_DIS_MATH_H_

#include "vec2.h"
#include "vec3.h"

#include <array>
#include <stdint.h>

typedef Vec3T<float> vec3;
typedef Vec2T<float> vec2;

typedef Vec2T<int32_t> ivec2;

/// matrix 3x3
/// left-to-right, top-to-bottom
/// m[0] m[1] m[2]
/// m[3] m[4] m[5]
/// m[6] m[7] m[8]
typedef std::array<float, 9> mat3;

static inline vec3 operator*(const mat3 &m, const vec3 &v)
{
    return vec3(m[0] * v.x + m[1] * v.y + m[2] * v.z,
                m[3] * v.x + m[4] * v.y + m[5] * v.z,
                m[6] * v.x + m[7] * v.y + m[8] * v.z);
}
static inline vec3 operator*(const mat3 &l, const mat3 &r)
{
    return vec3(l[0] * r[0] + l[1] * r[3] + l[2] * r[6],
                l[3] * r[1] + l[4] * r[4] + l[5] * r[7],
                l[6] * r[2] + l[7] * r[5] + l[8] * r[8]);
}
static inline mat3 transpose(const mat3 &m)
{
    return mat3{
        m[0],
        m[3],
        m[6],
        m[1],
        m[4],
        m[7],
        m[2],
        m[5],
        m[8],
    };
}
static inline float det(const mat3 &m)
{
    return m[0] * m[4] * m[8] - m[0] * m[5] * m[7] + m[1] * m[5] * m[6] -
           m[1] * m[3] * m[8] + m[2] * m[3] * m[7] - m[2] * m[4] * m[6];
}

static inline mat3 invert(const mat3 &m)
{
    float D = det(m);
    float invD = 1.0f / D;
    return mat3{
        (m[4] * m[8] - m[7] * m[5]) * invD,
        (m[7] * m[2] - m[1] * m[8]) * invD,
        (m[1] * m[5] - m[4] * m[2]) * invD,
        (m[6] * m[5] - m[3] * m[8]) * invD,
        (m[0] * m[8] - m[6] * m[2]) * invD,
        (m[3] * m[2] - m[0] * m[5]) * invD,
        (m[3] * m[7] - m[6] * m[4]) * invD,
        (m[6] * m[1] - m[0] * m[7]) * invD,
        (m[0] * m[4] - m[3] * m[1]) * invD,
    };
}

/// matrix 2x2
/// left-to-right, top-to-bottom
/// m[0] m[1]
/// m[2] m[3]
typedef std::array<float, 4> mat2;

static inline vec2 operator*(const mat2 &m, const vec2 &v)
{
    return vec2(m[0] * v.x + m[1] * v.y, m[2] * v.x + m[3] * v.y);
}
static inline vec2 operator*(const mat2 &l, const mat2 &r)
{
    return vec2(l[0] * r[0] + l[1] * r[2], l[2] * r[1] + l[3] * r[3]);
}
static inline mat2 transpose(const mat2 &m)
{
    return mat2{m[0], m[2], m[1], m[3]};
}
static inline float det(const mat2 &m) { return m[0] * m[3] - m[1] * m[2]; }
static inline mat2 invert(const mat2 &m)
{
    float D = det(m);
    float invD = 1.0f / D;

    return mat2{m[3] * invD, -m[1] * invD, -m[2] * invD, m[0] * invD};
}

#endif // _DIS_DIS_MATH_H_
