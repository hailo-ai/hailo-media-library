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
 * @file dewarp.h
 * @brief Dewarp and Crop-Resize functionality
 *
 * Contains buffer types, and declaration of a processing function that uses the
 *mesh grids to produce a dewarped image, as well as a crop and resize function.
 **/

#ifndef _DEWARP_DEWARP_H_
#define _DEWARP_DEWARP_H_

#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus

/**
 * Some defines for Mesh Square Size and
 * color discretization.
 */
#define MESH_CELL_SIZE_PIX (64) /**< Output cell size in pixels. */
#define COLOR_DISCRETIZATION (64)
#define CROP_AND_RESIZE_OUTPUTS_COUNT (5)
/** Dewarp mesh fractional bits. */
#define MESH_FRACT_BITS (16)

    /*! Type of color interpolation. */
    enum ColorInterpolation
    {
        BILINEAR_COLOR_INTERPOLATION = 0, /*!< Bilinear Color Interpolation. */
        BICUBIC_COLOR_INTERPOLATION = 1,  /*!< Bicubic Color Interpolation. */
    };

    /**
     * Grid of pixel coordinates in the input image, corresponding to even grid
     * in the output image. The grid cells in the output image are squares with
     * size MESH_CELL_SIZE_PIX. mesh_width/height are calculated sch that the
     * mesh to cover the whole output image. The most right and/or bottom
     * vertexes may be outside the image
     */
    typedef struct
    {
        int mesh_width;  /**< Number of vertexes in horizontal. */
        int mesh_height; /**< Number of vertexes in vertical. */
        int *mesh_table; /**< Pointer to vertexes, ordered x,y,x,y,.... */
                         /**  numbers are Q15.16. */
    } DewarpT;

    /** A structure defining a YUV420sp buffer. */
    typedef struct
    {
        unsigned char *y;  /**< Pointer to y plain. */
        unsigned char *uv; /**< Pointer to uv plain. */
        int width;         /**< Width, pixels. */
        int height;        /**< Height, pixels. */
        int bpln;          /**< Bytes per line (difference between 2 successive rows in
                              memory). */
    } BufT;

    /// @param dewarp Mesh buffer
    /// @param data_memory0 Temp DRAM0 memory pointer
    /// @param data_memory1 Temp DRAM1 memory pointer
    /// @param input Input YUV buffer pointer
    /// @param output Output YUV buffer pointer
    /// @param color_int_mode Color interpolation mode to use
    void dewarp_process(DewarpT *dewarp, void *data_memory0, void *data_memory1,
                        BufT *input, BufT *output, int color_int_mode);

    /// @param data_memory0 Temp DRAM0 memory pointer
    /// @param data_memory1 Temp DRAM1 memory pointer
    /// @param crop_width Crop width
    /// @param crop_height Crop height
    /// @param crop_up_left_x Crop Upper Left Coordinate X
    /// @param crop_up_left_y Crop Upper Left Coordinate Y
    /// @param input Input YUV buffer pointer
    /// @param cropped Cropped YUV buffers pointer
    /// @param color_int_mode Color interpolation mode to use
    void crop_and_resize_process(void *data_memory0, void *data_memory1,
                                 int crop_width, int crop_height,
                                 int crop_up_left_x, int crop_up_left_y,
                                 BufT *input,
                                 BufT *cropped[CROP_AND_RESIZE_OUTPUTS_COUNT],
                                 int color_int_mode);

    /// dewarp_required_mem()
    ///
    /// @brief returns the necessary memory in bytes for data_memory0 and
    /// data_memory1
    //  The arguments to this func may change.
    int dewarp_required_mem(void);

#ifdef __cplusplus
};
#endif // __cplusplus

#endif // _DEWARP_DEWARP_H_
