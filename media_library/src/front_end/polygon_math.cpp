#include "opencv2/imgproc.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/highgui.hpp"
#include <bitset>
#include <iostream>
#include <time.h>

#include <vector>
#include <algorithm>
#include <cmath>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <float.h>
#include <fstream>
#include <numbers>

#include "opencv2/core/core.hpp"
#include "opencv2/core/types_c.h"
#include <opencv2/core/mat.hpp>
#include "media_library_utils.hpp"
#include "media_library_logger.hpp"
#include "polygon_math.hpp"

using namespace cv;

// Local definitions - Use enum becuase they are only valid within the scope in which they are defined
enum
{
    CV_AA = 16,
    XY_SHIFT = 16,
    XY_ONE = 1 << XY_SHIFT,
    DRAWING_STORAGE_BLOCK = (1 << 12) - 256
};

struct PolyEdge
{
    PolyEdge() : y0(0), y1(0), x(0), dx(0), next(0)
    {
    }

    int y0, y1;
    int x, dx;
    PolyEdge *next;
};

struct CmpEdges
{
    bool operator()(const PolyEdge &e1, const PolyEdge &e2)
    {
        return e1.y0 - e2.y0 ? e1.y0 < e2.y0 : e1.x - e2.x ? e1.x < e2.x : e1.dx < e2.dx;
    }
};

/**
 * Fills a packaged array with a line segment.
 *
 * The binary image is represented as a vector of bytes (packaged_array),
 * where each byte represents 8 pixels in the image.
 * The line is specified by its y-coordinate (y) and the x-coordinates of its start and end points (x1 and x2).
 *
 * @param width The width of the array.
 * @param y The y-coordinate of the line.
 * @param x1 The starting x-coordinate of the line.
 * @param x2 The ending x-coordinate of the line.
 * @param packaged_array The vector representing the packaged array.
 */
static void fill_packaged_array_with_line(uint width, uint y, uint x1, uint x2, std::vector<uint8_t> &packaged_array)
{
    uint offset_mod, packaged_array_offset, num_of_bytes, bytes_mod = 0;
    uint8_t byte_mask;
    int num_of_pixels = (x2 - x1) + 1;

    // Create a mask for the first byte
    offset_mod = (y * width + x1) % 8;

    num_of_pixels -= (8 - offset_mod);
    // Byte mask is 255 shifted (right) by the offset
    byte_mask = 255 >> offset_mod;
    if (num_of_pixels < 0)
    {
        // Not a full byte left - we should zero num_of_pixels bits from the byte_mask:
        // mask = 1 << num_of_pixels * (-1)
        // mask = mask -1
        uint8_t mask = (1 << (num_of_pixels * -1)) - 1;

        // byte_mask = byte_mask AND (NOT mask)
        byte_mask &= ~mask;
        num_of_pixels = 0;
    }

    // Calculate the offset in the packaged array
    packaged_array_offset = (y * width + x1) / 8;
    // Set the first byte of the row
    packaged_array[packaged_array_offset] |= byte_mask;

    num_of_bytes = num_of_pixels / 8;

    // memset the full bytes with 255
    if (num_of_pixels >= 8)
        memset(&packaged_array[packaged_array_offset + 1], 255, num_of_bytes);

    bytes_mod = num_of_pixels % 8;
    // Create a mask for the last byte
    byte_mask = 255 << (8 - bytes_mod);
    // Set the last byte of the row
    packaged_array[(packaged_array_offset + num_of_bytes + 1)] |= byte_mask;
}

static void fill_edge_collection(Mat &img, std::vector<PolyEdge> &edges, uint stride,
                                 std::vector<uint8_t> &packaged_array)
{
    PolyEdge tmp;
    int i, y, total = (int)edges.size();
    Size size = img.size();
    PolyEdge *e;
    int y_max = INT_MIN, y_min = INT_MAX;
    int64 x_max = 0xFFFFFFFFFFFFFFFF, x_min = 0x7FFFFFFFFFFFFFFF;

    if (total < 2)
        return;

    for (i = 0; i < total; i++)
    {
        PolyEdge &e1 = edges[i];
        CV_Assert(e1.y0 < e1.y1);
        // Determine x-coordinate of the end of the edge.
        // (This is not necessary x-coordinate of any vertex in the array.)
        int64 x1 = e1.x + (e1.y1 - e1.y0) * e1.dx;
        y_min = std::min<int>(y_min, e1.y0);
        y_max = std::max<int>(y_max, e1.y1);
        x_min = std::min<int>(x_min, e1.x);
        x_max = std::max<int>(x_max, e1.x);
        x_min = std::min<int>(x_min, x1);
        x_max = std::max<int>(x_max, x1);
    }

    if (y_max < 0 || y_min >= size.height || x_max < 0 || x_min >= ((int64)size.width << XY_SHIFT))
        return;

    std::sort(edges.begin(), edges.end(), CmpEdges());

    // start drawing
    tmp.y0 = INT_MAX;
    edges.push_back(tmp); // after this point we do not add
                          // any elements to edges, thus we can use pointers
    i = 0;
    tmp.next = 0;
    e = &edges[i];
    y_max = MIN(y_max, size.height);

    for (y = e->y0; y < y_max; y++)
    {
        PolyEdge *last, *prelast, *keep_prelast;
        int draw = 0;
        int clipline = y < 0;

        prelast = &tmp;
        last = tmp.next;
        while (last || e->y0 == y)
        {
            if (last && last->y1 == y)
            {
                // exclude edge if y reaches its lower point
                prelast->next = last->next;
                last = last->next;
                continue;
            }
            keep_prelast = prelast;
            if (last && (e->y0 > y || last->x < e->x))
            {
                // go to the next edge in active list
                prelast = last;
                last = last->next;
            }
            else if (i < total)
            {
                // insert new edge into active list if y reaches its upper point
                prelast->next = e;
                e->next = last;
                prelast = e;
                e = &edges[++i];
            }
            else
                break;

            if (draw)
            {
                if (!clipline)
                {
                    // convert x's from fixed-point to image coordinates
                    int x1, x2;

                    if (keep_prelast->x > prelast->x)
                    {
                        x1 = (int)((prelast->x + XY_ONE - 1) >> XY_SHIFT);
                        x2 = (int)(keep_prelast->x >> XY_SHIFT);
                    }
                    else
                    {
                        x1 = (int)((keep_prelast->x + XY_ONE - 1) >> XY_SHIFT);
                        x2 = (int)(prelast->x >> XY_SHIFT);
                    }

                    // clip and draw the line
                    if (x1 < size.width && x2 >= 0)
                    {
                        if (x1 < 0)
                            x1 = 0;
                        if (x2 >= size.width)
                            x2 = size.width - 1;

                        fill_packaged_array_with_line(stride, y, x1, x2, packaged_array);
                    }
                }
                keep_prelast->x += keep_prelast->dx;
                prelast->x += prelast->dx;
            }
            draw ^= 1;
        }

        // sort edges (using bubble sort)
        keep_prelast = 0;

        do
        {
            prelast = &tmp;
            last = tmp.next;
            PolyEdge *last_exchange = 0;

            while (last != keep_prelast && last->next != 0)
            {
                PolyEdge *te = last->next;

                // swap edges
                if (last->x > te->x)
                {
                    prelast->next = te;
                    last->next = te->next;
                    te->next = last;
                    prelast = te;
                    last_exchange = prelast;
                }
                else
                {
                    prelast = last;
                    last = te;
                }
            }
            if (last_exchange == NULL)
                break;
            keep_prelast = last_exchange;
        } while (keep_prelast != tmp.next && keep_prelast != &tmp);
    }
}

static std::vector<Point> convert_vertices_to_points(const std::vector<privacy_mask_types::vertex> &vertices,
                                                     roi_t &roi, const uint &frame_width, const uint &frame_height)
{
    int min_x = INT_MAX;
    int min_y = INT_MAX;
    int max_x = 0;
    int max_y = 0;

    std::vector<Point> points;
    points.reserve(vertices.size());

    for (const auto &vertex : vertices)
    {
        int x = vertex.x;
        int y = vertex.y;

        Point point(x * PRIVACY_MASK_QUANTIZATION, y * PRIVACY_MASK_QUANTIZATION);
        points.emplace_back(point);

        min_x = std::min(min_x, point.x);
        min_y = std::min(min_y, point.y);
        max_x = std::max(max_x, point.x);
        max_y = std::max(max_y, point.y);
    }

    // CLIP min and max to be within the bitmask
    min_x = std::clamp(min_x, 0, (int)(frame_width * PRIVACY_MASK_QUANTIZATION));
    min_y = std::clamp(min_y, 0, (int)(frame_height * PRIVACY_MASK_QUANTIZATION));
    max_x = std::clamp(max_x, 0, (int)(frame_width * PRIVACY_MASK_QUANTIZATION));
    max_y = std::clamp(max_y, 0, (int)(frame_height * PRIVACY_MASK_QUANTIZATION));

    roi.x = min_x;
    roi.y = min_y;
    roi.width = max_x - min_x;
    roi.height = max_y - min_y;

    return points;
}

static void collect_poly_edges(const Point2l *v, int count, std::vector<PolyEdge> &edges, int line_type, int shift,
                               Point offset)
{
    int i, delta = offset.y + ((1 << shift) >> 1);
    Point2l pt0 = v[count - 1], pt1;
    pt0.x = (pt0.x + offset.x) << (XY_SHIFT - shift);
    pt0.y = (pt0.y + delta) >> shift;

    edges.reserve(edges.size() + count);

    for (i = 0; i < count; i++, pt0 = pt1)
    {
        Point2l t0, t1;
        PolyEdge edge;

        pt1 = v[i];
        pt1.x = (pt1.x + offset.x) << (XY_SHIFT - shift);
        pt1.y = (pt1.y + delta) >> shift;

        if (line_type < CV_AA)
        {
            t0.y = pt0.y;
            t1.y = pt1.y;
            t0.x = (pt0.x + (XY_ONE >> 1)) >> XY_SHIFT;
            t1.x = (pt1.x + (XY_ONE >> 1)) >> XY_SHIFT;
        }
        else
        {
            t0.x = pt0.x;
            t1.x = pt1.x;
            t0.y = pt0.y << XY_SHIFT;
            t1.y = pt1.y << XY_SHIFT;
        }

        if (pt0.y == pt1.y)
            continue;

        if (pt0.y < pt1.y)
        {
            edge.y0 = (int)(pt0.y);
            edge.y1 = (int)(pt1.y);
            edge.x = pt0.x;
        }
        else
        {
            edge.y0 = (int)(pt1.y);
            edge.y1 = (int)(pt0.y);
            edge.x = pt1.x;
        }
        edge.dx = (pt1.x - pt0.x) / (pt1.y - pt0.y);
        edges.push_back(edge);
    }
}

void scalar_to_raw_data(const Scalar &s, void *_buf, int type, int unroll_to)
{
    int i, depth = CV_MAT_DEPTH(type), cn = CV_MAT_CN(type);
    CV_Assert(cn <= 4);
    switch (depth)
    {
    case CV_8U: {
        uchar *buf = (uchar *)_buf;
        for (i = 0; i < cn; i++)
            buf[i] = saturate_cast<uchar>(s.val[i]);
        for (; i < unroll_to; i++)
            buf[i] = buf[i - cn];
    }
    break;
    default:
        CV_Error(CV_StsUnsupportedFormat, "");
    }
}

void fill_poly_internal(InputOutputArray _img, const Point **pts, const int *npts, int ncontours, const Scalar &color,
                        int line_type, int shift, Point offset, uint stride, std::vector<uint8_t> &packaged_array)
{

    Mat img = _img.getMat();

    if (line_type == CV_AA && img.depth() != CV_8U)
        line_type = 8;

    CV_Assert(pts && npts && ncontours >= 0 && 0 <= shift && shift <= XY_SHIFT);

    double buf[4];
    scalar_to_raw_data(color, buf, img.type(), 0);

    std::vector<PolyEdge> edges;

    int i, total = 0;
    for (i = 0; i < ncontours; i++)
        total += npts[i];

    edges.reserve(total + 1);
    for (i = 0; i < ncontours; i++)
    {
        std::vector<Point2l> _pts(pts[i], pts[i] + npts[i]);
        collect_poly_edges(_pts.data(), npts[i], edges, line_type, shift, offset);
    }

    fill_edge_collection(img, edges, stride, packaged_array);
}

media_library_return fill_poly_packaged_array(InputOutputArray img, InputArrayOfArrays pts, const Scalar &color,
                                              int lineType, int shift, uint stride,
                                              std::vector<uint8_t> &packaged_array)
{

    bool manyContours = pts.kind() == _InputArray::STD_VECTOR_VECTOR || pts.kind() == _InputArray::STD_VECTOR_MAT;
    int i, ncontours = manyContours ? (int)pts.total() : 1;
    if (ncontours == 0)
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    AutoBuffer<Point *> _ptsptr(ncontours);
    AutoBuffer<int> _npts(ncontours);
    Point **ptsptr = _ptsptr.data();
    int *npts = _npts.data();

    for (i = 0; i < ncontours; i++)
    {
        Mat p = pts.getMat(manyContours ? i : -1);
        CV_Assert(p.checkVector(2, CV_32S) >= 0);
        ptsptr[i] = p.ptr<Point>();
        npts[i] = p.rows * p.cols * p.channels() / 2;
    }
    fill_poly_internal(img, (const Point **)ptsptr, npts, (int)ncontours, color, lineType, shift, Point(), stride,
                       packaged_array);
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

privacy_mask_types::yuv_color_t rgb_to_yuv(const privacy_mask_types::rgb_color_t &rgb_color)
{
    privacy_mask_types::yuv_color_t yuv_color;
    yuv_color.y = 0.257 * rgb_color.r + 0.504 * rgb_color.g + 0.098 * rgb_color.b + 16;
    yuv_color.u = -0.148 * rgb_color.r - 0.291 * rgb_color.g + 0.439 * rgb_color.b + 128;
    yuv_color.v = 0.439 * rgb_color.r - 0.368 * rgb_color.g - 0.071 * rgb_color.b + 128;
    return yuv_color;
}

media_library_return rotate_polygon(privacy_mask_types::PolygonPtr polygon, double rotation_angle, uint frame_width,
                                    uint frame_height)
{
    double xm = static_cast<double>(frame_width) / 2;
    double ym = static_cast<double>(frame_height) / 2;

    // Convert to Radians
    rotation_angle = rotation_angle * std::numbers::pi / 180.0;
    for (auto &vertex : polygon->vertices)
    {
        double trans_x = vertex.x - xm;
        double trans_y = ym - vertex.y;
        double x_res = (trans_x * std::cos(rotation_angle) + trans_y * std::sin(rotation_angle)) + xm;
        double y_res = ym - ((-1) * trans_x * std::sin(rotation_angle) + trans_y * std::cos(rotation_angle));
        vertex.x = x_res;
        vertex.y = y_res;
    }

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return rotate_polygons(std::vector<privacy_mask_types::PolygonPtr> &polygons, double rotation_angle,
                                     uint frame_width, uint frame_height)
{
    for (auto &polygon : polygons)
    {
        if (rotate_polygon(polygon, rotation_angle, frame_width, frame_height) !=
            media_library_return::MEDIA_LIBRARY_SUCCESS)
            return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return write_polygons_to_privacy_mask_data(std::vector<privacy_mask_types::PolygonPtr> &polygons,
                                                         const uint &frame_width, const uint &frame_height,
                                                         const privacy_mask_types::rgb_color_t &color,
                                                         privacy_mask_types::PrivacyMaskDataPtr privacy_mask_data)
{
    struct timespec start_fill_polly, end_fill_polly;
    clock_gettime(CLOCK_MONOTONIC, &start_fill_polly);

    // Quantize the frame size
    int mask_width = frame_width * PRIVACY_MASK_QUANTIZATION;
    int mask_height = frame_height * PRIVACY_MASK_QUANTIZATION;

    // Initialize a matrix of zeros
    Mat mask = Mat::zeros(Size(mask_width, mask_height), CV_8UC1);
    auto white = Scalar(255, 255, 255);

    // Round up frame_width to byte_size / quantization (32)
    int line_division = 8 / PRIVACY_MASK_QUANTIZATION;
    int bytes_per_line = (frame_width / line_division + 7) & ~7;
    // Handle padding (aligned to 8)
    int mask_width_with_stride = bytes_per_line * 8;

    // To represent a bit per pixel structure - set packaged array size to /8 and add one if it is not a multiple of 8
    uint packaged_array_size =
        (mask_width_with_stride * mask_height) / 8 + ((mask_width_with_stride * mask_height) % 8 == 0 ? 0 : 1);
    std::vector<uint8_t> packaged_array(packaged_array_size, 0);

    // Set the rois count and YUV color in the privacy mask data
    privacy_mask_data->color = rgb_to_yuv(color);

    uint i = 0;
    for (const auto &polygon : polygons)
    {
        roi_t roi;
        std::vector<Point> pts = convert_vertices_to_points(polygon->vertices, roi, frame_width, frame_height);

        if (!(roi.width > 0 && roi.height > 0)) // if ROI is out of frame, ignore it
        {
            continue;
        }

        privacy_mask_data->rois[i] = roi;
        if (fill_poly_packaged_array(mask, pts, white, 8, 0, mask_width_with_stride, packaged_array) !=
            media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("Failed to fill polygon");
            return media_library_return::MEDIA_LIBRARY_ERROR;
        }
        i++;
    }
    privacy_mask_data->rois_count = i;

    if (privacy_mask_data->bitmask->buffer_data->planes[0].bytesused != packaged_array_size)
    {
        LOGGER__ERROR("Failed to fill polygon - privacy mask buffer size is not equal to the packaged array size");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }
    // memcopy packaged array to privacy_mask_data void pointer bit mask
    memcpy(privacy_mask_data->bitmask->get_plane_ptr(0), packaged_array.data(), packaged_array_size);

    clock_gettime(CLOCK_MONOTONIC, &end_fill_polly);
    [[maybe_unused]] long ms = (long)media_library_difftimespec_ms(end_fill_polly, start_fill_polly);
    LOGGER__DEBUG("perform fill polygon took {} milliseconds ({} fps)", ms, (1000 / ms));

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}
