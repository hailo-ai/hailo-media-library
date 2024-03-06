/*
 * Copyright (c) 2017-2024 Hailo Technologies Ltd. All rights reserved.
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
 * @file vec3.h
 * @brief Definition and methods for a 3-dimensional vector type used by the
 *digital image stabilization library.
 **/
#ifndef _DIS_VEC3_H_
#define _DIS_VEC3_H_

#include <math.h>

template <typename ValT>
struct Vec3T
{
    ValT x, y, z;

    Vec3T(){};
    Vec3T(ValT x_, ValT y_, ValT z_) : x(x_), y(y_), z(z_){};

    Vec3T operator-() const { return Vec3T(-x, -y, -z); }
    // Operations with vectors
    Vec3T &operator+=(const Vec3T &r)
    {
        x += r.x;
        y += r.y;
        z += r.z;
        return *this;
    }
    Vec3T &operator-=(const Vec3T &r)
    {
        x -= r.x;
        y -= r.y;
        z -= r.z;
        return *this;
    }
    Vec3T operator+(const Vec3T &r) const
    {
        return Vec3T(x + r.x, y + r.y, z + r.z);
    }
    Vec3T operator-(const Vec3T &r) const
    {
        return Vec3T(x - r.x, y - r.y, z - r.z);
    }
    Vec3T product(const Vec3T &r) const
    {
        return Vec3T(x * r.x, y * r.y, z * r.z);
    }
    // Operations with scalar
    Vec3T &operator*=(ValT r)
    {
        x *= r;
        y *= r;
        z *= r;
        return *this;
    }
    Vec3T operator*(ValT r) const { return Vec3T(x * r, y * r, z * r); }
    Vec3T &operator+=(ValT r)
    {
        x += r;
        y += r;
        z += r;
        return *this;
    }
    Vec3T operator+(ValT r) const { return Vec3T(x + r, y + r, z + r); }
    Vec3T &operator/=(ValT r)
    {
        ValT inv_r = 1 / r;
        x *= inv_r;
        y *= inv_r;
        z *= inv_r;
        return *this;
    }
    Vec3T operator/(ValT r) const
    {
        ValT inv_r = 1 / r;
        return Vec3T(x * inv_r, y * inv_r, z * inv_r);
    }
    // comparison operators
    bool operator==(Vec3T const &v) const { return x == v.x && y == v.y; }
    // geometrical operations
    ValT dot(const Vec3T &r) const { return x * r.x + y * r.y; }
    Vec3T cross(const Vec3T &r) const
    {
        return Vec3T(y * r.z - z * r.y, z * r.x - x * r.z, x * r.y - y * r.x);
    }
    ValT len() const { return std::sqrt(x * x + y * y); }
    ValT len2() const { return x * x + y * y; }
    ValT rlen() const { return 1.f / std::sqrt(len2()); }
    void normalize() { (*this) *= rlen(); }
    Vec3T lenalized() const { return (*this) * rlen(); }
};

#endif // _DIS_VEC3_H_
