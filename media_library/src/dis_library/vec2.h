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
 * @file vec2.h
 * @brief Definition and methods for a 2-dimensional vector type used by the
 *digital image stabilization library.
 **/
#ifndef _DIS_VEC2_H_
#define _DIS_VEC2_H_

#include <cmath>
template <typename ValT> struct Vec2T
{
    ValT x, y;

    Vec2T() {};
    Vec2T(ValT x_, ValT y_) : x(x_), y(y_) {};

    Vec2T operator-() const
    {
        return Vec2T(-x, -y);
    }
    // Operations with vectors
    Vec2T &operator+=(const Vec2T &r)
    {
        x += r.x;
        y += r.y;
        return *this;
    }
    Vec2T &operator-=(const Vec2T &r)
    {
        x -= r.x;
        y -= r.y;
        return *this;
    }
    Vec2T operator+(const Vec2T &r) const
    {
        return Vec2T(x + r.x, y + r.y);
    }
    Vec2T operator-(const Vec2T &r) const
    {
        return Vec2T(x - r.x, y - r.y);
    }
    Vec2T product(const Vec2T &r) const
    {
        return Vec2T(x * r.x, y * r.y);
    }
    // Operations with scalar
    Vec2T &operator*=(ValT r)
    {
        x *= r;
        y *= r;
        return *this;
    }
    Vec2T operator*(ValT r) const
    {
        return Vec2T(x * r, y * r);
    }
    Vec2T &operator+=(ValT r)
    {
        x += r;
        y += r;
        return *this;
    }
    Vec2T operator+(ValT r) const
    {
        return Vec2T(x + r, y + r);
    }
    Vec2T &operator/=(ValT r)
    {
        ValT inv_r = 1 / r;
        x *= inv_r;
        y *= inv_r;
        return *this;
    }
    Vec2T operator/(ValT r) const
    {
        ValT inv_r = 1 / r;
        return Vec2T(x * inv_r, y * inv_r);
    }
    // comparison operators
    bool operator==(Vec2T const &v) const
    {
        return x == v.x && y == v.y;
    }
    // geometrical operations
    ValT dot(const Vec2T &r) const
    {
        return x * r.x + y * r.y;
    }
    ValT cross(const Vec2T &r) const
    {
        return x * r.y - y * r.x;
    }
    ValT len() const
    {
        return std::sqrt(x * x + y * y);
    }
    ValT len2() const
    {
        return x * x + y * y;
    }
    ValT inv_len() const
    {
        return 1.f / std::sqrt(len2());
    }
    void normalize()
    {
        (*this) *= inv_len();
    }
    Vec2T lenalized() const
    {
        return (*this) * inv_len();
    }
};

#endif // _DIS_VEC2_H_
