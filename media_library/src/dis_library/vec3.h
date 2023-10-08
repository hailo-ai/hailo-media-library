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
* @file vec3.h
* @brief Definition and methods for a 3-dimensional vector type used by the digital
* image stabilization library.
**/
#ifndef _DIS_VEC3_H_
#define _DIS_VEC3_H_

#include <math.h>

template <typename ValT>
struct Vec3T
{
    ValT x,y,z;

    Vec3T(){};
    Vec3T( ValT x_, ValT y_, ValT z_) :x(x_),y(y_),z(z_) {};

    Vec3T operator -() const    {
        return Vec3T(-x, -y, -z);
    }
    // Operations with vectors
    Vec3T& operator +=(const Vec3T& r)	{
    	x += r.x; y += r.y;  z += r.z; return *this;
	}
    Vec3T& operator -=(const Vec3T& r)	{
    	x -= r.x; y -= r.y; z -= r.z; return *this;
	}
    Vec3T operator +(const Vec3T& r) const    {
    	return Vec3T(x+r.x, y+r.y, z+r.z);
    }
    Vec3T operator -(const Vec3T& r) const    {
        return Vec3T(x-r.x, y-r.y, z-r.z);
    }
    Vec3T product(const Vec3T& r) const    {
    	return Vec3T(x*r.x, y*r.y, z*r.z);
    }
    // Operations with scalar
    Vec3T& operator *=(ValT r)	{
    	x *= r; y *= r; z *= r; return *this;
	}
    Vec3T operator *(ValT r) const    {
    	return Vec3T(x*r, y*r, z*r);
    }
    Vec3T& operator +=(ValT r)	{
    	x += r; y += r; z += r; return *this;
	}
    Vec3T operator +(ValT r) const    {
    	return Vec3T(x+r, y+r, z+r);
    }
	Vec3T& operator /=(ValT r)	{
        ValT inv_r = 1 / r;
		x *= inv_r; y *= inv_r; z *= inv_r; return *this;
	}
    Vec3T operator /(ValT r) const    {
        ValT inv_r = 1 / r;
    	return Vec3T(x*inv_r, y*inv_r, z*inv_r);
    }
    //comparison operators
    bool operator == (Vec3T const &v) const {
        return x == v.x && y == v.y;
    }
    //geometrical operations
    ValT dot(const Vec3T& r) const    {
    	return x*r.x + y*r.y;
    }
    Vec3T cross(const Vec3T& r) const	{
        return Vec3T( y*r.z - z*r.y, z*r.x - x*r.z, x*r.y - y*r.x );
	}
    ValT len() const    {
    	return std::sqrt(x*x + y*y);
    }
	ValT len2() const	{
		return x*x + y*y;
	}
	ValT rlen() const	{
		return 1.f / std::sqrt(len2());
	}
    void normalize()    {
    	(*this) *= rlen();
    }
	Vec3T lenalized() const	{
		return (*this) * rlen();
	}

};

#endif // _DIS_VEC3_H_
