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
#include "dsp_utils.hpp"
#include "media_library_logger.hpp"

/** @defgroup dsp_utils_definitions MediaLibrary DSP utilities CPP API
 * definitions
 *  @{
 */

namespace dsp_utils
{
    static dsp_device device = NULL;
    static uint dsp_device_refcount = 0;

    /**
     * Create a DSP device, and store it globally
     * The function requests a device from the DSP library.
     * @return dsp_status
     */
    static dsp_status create_device()
    {
        dsp_status create_device_status = DSP_UNINITIALIZED;
        // If the device is not initialized, initialize it, else return SUCCESS
        if (device == NULL)
        {
            LOGGER__INFO("Creating dsp device");
            create_device_status = dsp_create_device(&device);
            if (create_device_status != DSP_SUCCESS)
            {
                LOGGER__ERROR("Open DSP device failed with status {}",
                              create_device_status);
                return create_device_status;
            }
        }

        return DSP_SUCCESS;
    }

    /**
     * Release the DSP device.
     * The function releases the device using the DSP library.
     * If there are other references to the device, just decrement the refcount and
     * skip the release.
     * @return dsp_status
     */
    dsp_status release_device()
    {
        if (device == NULL)
        {
            LOGGER__WARNING("Release device skipped: Dsp device is already NULL");
            return DSP_SUCCESS;
        }

        dsp_device_refcount--;
        if (dsp_device_refcount > 0)
        {
            LOGGER__DEBUG("Release dsp device skipped, refcount is {}",
                          dsp_device_refcount);
        }
        else
        {
            LOGGER__DEBUG("Releasing dsp device, refcount is {}",
                          dsp_device_refcount);
            dsp_status status = dsp_release_device(device);
            if (status != DSP_SUCCESS)
            {
                LOGGER__ERROR("Release device failed with status {}", status);
                return status;
            }
            device = NULL;
            LOGGER__INFO("Dsp device released successfully");
        }

        return DSP_SUCCESS;
    }

    /**
     * Acquire the DSP device.
     * This function creates the DSP device using the DSP library once, and then
     * increases the reference count.
     *
     * @return dsp_status
     */
    dsp_status acquire_device()
    {
        if (device == NULL)
        {
            dsp_status status = create_device();
            if (status != DSP_SUCCESS)
                return status;
        }
        dsp_device_refcount++;
        LOGGER__DEBUG("Acquired dsp device, refcount is {}", dsp_device_refcount);
        return DSP_SUCCESS;
    }

    /**
     * Create a buffer on the DSP
     * The function requests a buffer from the DSP library.
     * The buffer can be used later for DSP operations.
     * @param[in] size the size of the buffer to create
     * @param[out] buffer a pointer to a buffer - DSP library will allocate the
     * buffer
     * @return dsp_status
     */
    dsp_status create_hailo_dsp_buffer(size_t size, void **buffer)
    {
        if (device != NULL)
        {
            LOGGER__DEBUG("Creating dsp buffer with size {}", size);
            dsp_status status = dsp_create_buffer(device, size, buffer);
            if (status != DSP_SUCCESS)
            {
                LOGGER__ERROR("Create buffer failed with status {}", status);
                return status;
            }
        }
        else
        {
            LOGGER__ERROR("Create buffer failed: device is NULL");
            return DSP_UNINITIALIZED;
        }

        return DSP_SUCCESS;
    }

    /**
     * Release a buffer allocated by the DSP
     * @param[in] buffer the buffer to release
     * @return dsp_status
     */
    dsp_status release_hailo_dsp_buffer(void *buffer)
    {
        if (device == NULL)
        {
            LOGGER__ERROR("DSP release buffer failed: device is NULL");
            return DSP_UNINITIALIZED;
        }

        LOGGER__DEBUG("Releasing dsp buffer");
        dsp_status status = dsp_release_buffer(device, buffer);
        if (status != DSP_SUCCESS)
        {
            LOGGER__ERROR("DSP release buffer failed with status {}", status);
            return status;
        }

        LOGGER__DEBUG("DSP buffer released successfully");
        return DSP_SUCCESS;
    }

    /**
     * Perform DSP crop and resize
     * The function calls the DSP library to perform crop and resize on a given
     * buffer. DSP will place the result in the output buffer.
     *
     * @param[in] input_image_properties input image properties
     * @param[out] output_image_properties output image properties
     * @param[in] args crop and resize arguments
     * @param[in] dsp_interpolation_type interpolation type to use
     * @return dsp_status
     */
    dsp_status
    perform_crop_and_resize(dsp_image_properties_t *input_image_properties,
                            dsp_image_properties_t *output_image_properties,
                            crop_resize_dims_t args,
                            dsp_interpolation_type_t dsp_interpolation_type)
    {
        if (device == NULL)
        {
            LOGGER__ERROR("Perform DSP crop and resize ERROR: Device is NULL");
            return DSP_UNINITIALIZED;
        }

        dsp_resize_params_t resize_params = {
            .src = input_image_properties,
            .dst = output_image_properties,
            .interpolation = dsp_interpolation_type,
        };

        dsp_status status;
        if (args.perform_crop)
        {
            dsp_crop_api_t crop_params = {
                .start_x = args.crop_start_x,
                .start_y = args.crop_start_y,
                .end_x = args.crop_end_x,
                .end_y = args.crop_end_y,
            };
            status = dsp_crop_and_resize(device, &resize_params, &crop_params);
        }
        else
        {
            status = dsp_resize(device, &resize_params);
        }

        if (status != DSP_SUCCESS)
        {
            LOGGER__ERROR("DSP Crop & resize command failed with status {}",
                          status);
            return status;
        }

        LOGGER__INFO("DSP Crop & resize command completed successfully");
        return DSP_SUCCESS;
    }

    /**
     * Perform multiple crop and resize on the DSP
     * The function calls the DSP library to perform crop and resize on a given
     * input buffer. DSP will place the results in the array of output buffer.
     *
     * @param[in] input_image_properties pointer input buffer
     * @param[in] dsp_interpolation_type interpolation type to use
     * @param[out] output_image_properties_array array of output buffers
     * @return dsp_status
     */
    dsp_status
    perform_dsp_multi_resize(dsp_multi_resize_params_t *multi_resize_params,
                             uint crop_start_x, uint crop_start_y, uint crop_end_x,
                             uint crop_end_y)
    {
        dsp_crop_api_t crop_params = {
            .start_x = crop_start_x,
            .start_y = crop_start_y,
            .end_x = crop_end_x,
            .end_y = crop_end_y,
        };

        return dsp_multi_crop_and_resize(device, multi_resize_params, &crop_params);
    }

    dsp_status perform_dsp_dewarp(dsp_image_properties_t *input_image_properties,
                                  dsp_image_properties_t *output_image_properties,
                                  dsp_dewarp_mesh_t *mesh,
                                  dsp_interpolation_type_t interpolation)
    {
        return dsp_dewarp(device, input_image_properties, output_image_properties,
                          mesh, interpolation);
    }

    /**
     * Perform DSP blending using multiple overlays
     * The function calls the DSP library to perform blending between one
     * main buffer and multiple overlay buffers.
     * DSP will blend the overlay buffers onto the image frame in place
     *
     * @param[in] image_frame pointer to input image to blend on
     * @param[in] overlay pointer to input images to overlay with
     * @param[in] overlays_count number of overlays to blend
     * @return dsp_status
     */
    dsp_status perform_dsp_multiblend(dsp_image_properties_t *image_frame,
                                      dsp_overlay_properties_t *overlay,
                                      size_t overlays_count)
    {
        return dsp_blend(device, image_frame, overlay, overlays_count);
    }

    /**
     * Free DSP struct resources
     *
     * @param[in] overlay_properties pointer to the properties to free
     * @return void
     */
    void free_overlay_property_planes(dsp_overlay_properties_t *overlay_properties)
    {
        free_image_property_planes(&(overlay_properties->overlay));
    }

    /**
     * Free DSP struct resources
     *
     * @param[in] image_properties pointer to the properties to free
     * @return void
     */
    void free_image_property_planes(dsp_image_properties_t *image_properties)
    {
        free(image_properties->planes);
    }
} // namespace dsp_utils

/** @} */ // end of dsp_utils_definitions