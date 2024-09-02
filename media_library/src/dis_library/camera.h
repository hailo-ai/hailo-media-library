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
 * @file camera.h
 * @brief Camera models
 *
 * Contains base camera class and its derived pinhole and fisheye classes.
 **/
#ifndef _DIS_CAMERA_H_
#define _DIS_CAMERA_H_

#include "dis_common.h"
#include "dis_math.h"

#include <algorithm>

struct Camera
{
    /// camera resolution
    ivec2 res{2, 2};
    /// optical center
    vec2 oc{1, 1};
    /// FoV in radians (default is diagonal)
    float fov = 0.f;
    /// focal length in pixels
    float flen = 1.f;
    /// image diagonal in pixels
    float diag = sqrt(2.f);

    /// left,top,right,bottom  half-FOVs. Used to calc the room for
    /// stabilization.
    std::array<float, 4> ltrb;
    std::array<float, 4> diag_ltrb;

    void set_ltrb(std::array<float, 4> ltrb_) { ltrb = ltrb_; }

    /// @brief Projects 3D ray in camera coordinate system onto the camera
    /// sensor. Inverse of point2ray().
    /// @param ray ray to be projected
    virtual vec2 ray2point(const vec3 &ray) const = 0;

    /// @brief Maps a point on the camera sensor to 3D camera coordinates.
    /// Inverse of ray2point().
    /// @param pt point on camera sensor
    virtual vec3 point2ray(const vec2 &pt) const = 0;

    virtual ~Camera(){};
};

/// PinHole Camera Model
struct PinHole : Camera
{
    PinHole(){};
    PinHole(float flen_, vec2 oc_, ivec2 res_)
    {
        flen = flen_;
        res = res_;
        oc = oc_;
        diag = std::hypotf(res.x, res.y);
        fov = 2 * std::atan2(0.5f * diag, flen);

        // calc input camera left,top,bottom,right FOVs
        ltrb[0] = atan2(oc.x, flen);
        ltrb[1] = atan2(oc.y, flen);
        ltrb[2] = atan2(res.x - oc.x, flen);
        ltrb[3] = atan2(res.y - oc.y, flen);

        // input camera diagonal FOVs as they appear clockwise starting form
        // left side - TL,TR,BR,BL
        diag_ltrb[0] = atan2(std::hypotf(oc.x, oc.y), flen);
        diag_ltrb[1] = atan2(std::hypotf(res.x - oc.x, oc.y), flen);
        diag_ltrb[2] = atan2(std::hypotf(res.x - oc.x, res.y - oc.y), flen);
        diag_ltrb[3] = atan2(std::hypotf(oc.x, res.y - oc.y), flen);
    }

    vec2 ray2point(const vec3 &ray) const override
    {
        if (ray.z <= 0)
            return vec2(NAN, NAN);
        return oc + vec2(ray.x, ray.y) * (flen / ray.z);
    }
    vec3 point2ray(const vec2 &pt) const override
    {
        return vec3(pt.x - oc.x, pt.y - oc.y, flen);
    }
};

/// FishEye Camera Model
struct FishEye : Camera
{

    /// theta2r distortion LUT parameters
    static constexpr int theta2r_size = 1025;
    static constexpr float theta_step = float(M_PI / float(theta2r_size - 1));
    static constexpr float inv_theta_step = 1.f / theta_step;
    float theta2r[theta2r_size];

    FishEye() { std::fill_n(theta2r, theta2r_size, 0.f); };
    FishEye(vec2 oc_, ivec2 res_, float (&theta2r_)[theta2r_size])
    {
        init(oc_, res_, theta2r_);
    }

    void init(vec2 oc_, ivec2 res_, float (&theta2r_)[theta2r_size])
    {
        res = res_;
        oc = oc_;
        std::copy_n(theta2r_, theta2r_size, theta2r);
        diag = vec2(res.x, res.y).len();
        flen = theta2r[1] / theta_step;
        fov = 2 * rad2theta(diag / 2);

        // calc input camera left,top,bottom,right FOVs
        ltrb[0] = rad2theta(oc.x);
        ltrb[1] = rad2theta(oc.y);
        ltrb[2] = rad2theta(res.x - oc.x);
        ltrb[3] = rad2theta(res.y - oc.y);

        // input camera diagonal FOVs as they appear clockwise starting form
        // left side - TL,TR,BR,BL
        diag_ltrb[0] = rad2theta(std::hypotf(oc.x, oc.y));
        diag_ltrb[1] = rad2theta(std::hypotf(res.x - oc.x, oc.y));
        diag_ltrb[2] = rad2theta(std::hypotf(res.x - oc.x, res.y - oc.y));
        diag_ltrb[3] = rad2theta(std::hypotf(oc.x, res.y - oc.y));
    };

    /// @brief Finds radius corresponding to an angle
    ///
    /// @param radius radius
    float rad2theta(float radius) const
    {
        int i = std::lower_bound(std::begin(theta2r), std::end(theta2r) - 2,
                                 radius) -
                std::begin(theta2r);
        return theta_step * (float(i) + (radius - theta2r[i]) /
                                            (theta2r[i + 1] - theta2r[i]));
    }
    /// @brief Finds angle corresponding to a radius
    ///
    /// @param theta angle in radians
    float theta2rad(float theta) const
    {
        float fi = theta * inv_theta_step;
        int i = clamp(int(fi), 0, theta2r_size - 2);
        fi -= i;
        return theta2r[i] * (1.f - fi) + theta2r[i + 1] * fi;
    }

    vec2 ray2point(const vec3 &ray) const override
    {
        vec2 pt(ray.x, ray.y);
        float rad = pt.len();
        if (rad == 0)
            return oc;
        float theta = atan2f(rad, ray.z);
        return oc + pt * (theta2rad(theta) / rad);
    }

    vec3 point2ray(const vec2 &pt) const override
    {
        vec2 pc(pt.x - oc.x, pt.y - oc.y);
        float rad = pc.len();
        float theta = rad2theta(rad);
        if (theta == 0)
            return vec3(0, 0, flen);
        return vec3(pc.x, pc.y, rad / std::tan(theta));
    }
};

#endif // _DIS_CAMERA_H_
