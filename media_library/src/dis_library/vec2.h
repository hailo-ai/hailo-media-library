/**
* Copyright 2020 (C) Hailo Technologies Ltd.
* All rights reserved.
*
* Hailo Technologies Ltd. ("Hailo") disclaims any warranties, including, but not limited to,
* the implied warranties of merchantability and fitness for a particular purpose.
* This software is provided on an "AS IS" basis, and Hailo has no obligation to provide maintenance,
* support, updates, enhancements, or modifications.
*
* You may use this software in the development of any project.
* You shall not reproduce, modify or distribute this software without prior written permission.
**/
/**
* @file vec2.h
* @brief Definition and methods for a 2-dimensional vector type used by the digital
* image stabilization library.
**/
#ifndef _DIS_VEC2_H_
#define _DIS_VEC2_H_

#include <cmath>
template <typename ValT>
struct Vec2T
{
    ValT x,y;

    Vec2T(){};
    Vec2T( ValT x_, ValT y_) :x(x_),y(y_) {};

    Vec2T operator -() const    {
        return Vec2T(-x, -y);
    }
    // Operations with vectors
    Vec2T& operator +=(const Vec2T& r)	{
    	x += r.x; y += r.y; return *this;
	}
    Vec2T& operator -=(const Vec2T& r)	{
    	x -= r.x; y -= r.y; return *this;
	}
    Vec2T operator +(const Vec2T& r) const    {
    	return Vec2T(x+r.x, y+r.y);
    }
    Vec2T operator -(const Vec2T& r) const    {
        return Vec2T(x-r.x, y-r.y);
    }
    Vec2T product(const Vec2T& r) const    {
    	return Vec2T(x*r.x, y*r.y);
    }
    // Operations with scalar
    Vec2T& operator *=(ValT r)	{
    	x *= r; y *= r; return *this;
	}
    Vec2T operator *(ValT r) const    {
    	return Vec2T(x*r, y*r);
    }
    Vec2T& operator +=(ValT r)	{
    	x += r; y += r; return *this;
	}
    Vec2T operator +(ValT r) const    {
    	return Vec2T(x+r, y+r);
    }
	Vec2T& operator /=(ValT r)	{
        ValT inv_r = 1 / r;
		x *= inv_r; y *= inv_r; return *this;
	}
    Vec2T operator /(ValT r) const    {
        ValT inv_r = 1 / r;
    	return Vec2T(x*inv_r, y*inv_r);
    }
    //comparison operators
    bool operator == (Vec2T const &v) const {
        return x == v.x && y == v.y;
    }
    //geometrical operations
    ValT dot(const Vec2T& r) const    {
    	return x*r.x + y*r.y;
    }
    ValT cross(const Vec2T& r) const	{
		return x*r.y - y*r.x;
	}
    ValT len() const    {
    	return std::sqrt(x*x + y*y);
    }
	ValT len2() const	{
		return x*x + y*y;
	}
	ValT inv_len() const	{
		return 1.f / std::sqrt(len2());
	}
    void normalize()    {
    	(*this) *= inv_len();
    }
	Vec2T lenalized() const	{
		return (*this) * inv_len();
	}

};

#endif // _DIS_VEC2_H_
