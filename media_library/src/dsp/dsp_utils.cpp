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
#include "dsp_utils.hpp"
#include "media_library_logger.hpp"
#include "media_library_types.hpp"
#include "dma_memory_allocator.hpp"

/** @defgroup dsp_utils_definitions MediaLibrary DSP utilities CPP API
 * definitions
 *  @{
 */

template <>
dsp_data_plane_t hailo_data_plane_t::As() const
{
    dsp_data_plane_t plane;
    plane.fd = fd;
    plane.bytesperline = bytesperline;
    plane.bytesused = bytesused;
    return plane;
}

template <>
hailo_dsp_buffer_data_t hailo_buffer_data_t::As() const
{
    return hailo_dsp_buffer_data_t(width, height, planes_count, format, memory, planes);
}

namespace dsp_utils
{
    static dsp_device device = NULL;
    static uint dsp_device_refcount = 0;

    /**
     * Create a DSP device and store it globally
     * This function requests a device from the DSP library.
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
     * This function releases the device using the DSP library.
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
     * This function requests a buffer from the DSP library.
     * The buffer can be used later for DSP operations.
     * @param[in] size the size of the buffer to create
     * @param[out] buffer a pointer to a buffer - DSP library will allocate the
     * buffer
     * @return dsp_status
     */
    dsp_status create_hailo_dsp_buffer(size_t size, void **buffer, bool dma)
    {
        if (device != NULL)
        {
            if (dma)
            {
                LOGGER__DEBUG("Creating dma buffer with size {}", size);
                // dsp_status status = dsp_create_dma_buffer(device, size, buffer);
                media_library_return status = DmaMemoryAllocator::get_instance().allocate_dma_buffer(size, buffer);
                if (status != MEDIA_LIBRARY_SUCCESS)
                {
                    LOGGER__ERROR("Create dma buffer failed with status {}", status);
                    return DSP_UNINITIALIZED;
                }
            }
            else
            {
                LOGGER__DEBUG("Creating dsp buffer with size {}", size);
                dsp_status status = dsp_create_buffer(device, size, buffer);
                if (status != DSP_SUCCESS)
                {
                    LOGGER__ERROR("Create buffer failed with status {}", status);
                    return status;
                }
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
     * Perform DSP Resize
     * This function calls the DSP library to perform resize on a given buffer.
     * DSP will place the result in the output buffer.
     *
     * @param[in] input_image_properties input image properties
     * @param[in] output_image_properties output image properties
     * @param[in] dsp_interpolation_type interpolation type to use
     * @param[in] use_letterbox should letterbox resize be used
     * @return dsp_status
     */
    dsp_status perform_resize(dsp_image_properties_t *input_image_properties, dsp_image_properties_t *output_image_properties, dsp_interpolation_type_t dsp_interpolation_type, std::optional<dsp_letterbox_properties_t> letterbox_properties)
    {
        if (device == NULL)
        {
            LOGGER__ERROR("Perform DSP crop and resize ERROR: Device is NULL");
            return DSP_UNINITIALIZED;
        }

        dsp_resize_params_t resize_params{
            .src = input_image_properties,
            .dst = output_image_properties,
            .interpolation = dsp_interpolation_type,
        };

        dsp_roi_t crop_params{
            .start_x = 0,
            .start_y = 0,
            .end_x = input_image_properties->width,
            .end_y = input_image_properties->height,
        };

        dsp_letterbox_properties_t letterbox_params;
        if (letterbox_properties.has_value())
        {
            letterbox_params = letterbox_properties.value();
        }
        else
        {
            letterbox_params.alignment = DSP_NO_LETTERBOX;
            letterbox_params.color.y = 0;
            letterbox_params.color.u = 128;
            letterbox_params.color.v = 128;
        }

        dsp_status status = dsp_crop_and_resize_letterbox(device, &resize_params, &crop_params, &letterbox_params);

        if (status != DSP_SUCCESS)
        {
            LOGGER__ERROR("DSP Resize command failed with status {}", status);
            return status;
        }

        LOGGER__INFO("DSP Resize command completed successfully");
        return DSP_SUCCESS;
    }

    /**
     * Perform DSP crop and resize
     * This function calls the DSP library to perform crop and resize on a given
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
                            dsp_interpolation_type_t dsp_interpolation_type,
                            std::optional<dsp_letterbox_properties_t> letterbox_properties)
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

        dsp_letterbox_properties_t letterbox_params;
        if (letterbox_properties.has_value())
        {
            letterbox_params = letterbox_properties.value();
        }
        else
        {
            letterbox_params.alignment = DSP_NO_LETTERBOX;
            letterbox_params.color.y = 0;
            letterbox_params.color.u = 128;
            letterbox_params.color.v = 128;
        }

        dsp_status status;
        if (args.perform_crop)
        {
            dsp_crop_api_t crop_params = {
                .start_x = args.crop_start_x,
                .start_y = args.crop_start_y,
                .end_x = args.crop_end_x,
                .end_y = args.crop_end_y,
            };
            status = dsp_crop_and_resize_letterbox(device, &resize_params, &crop_params, &letterbox_params);
        }
        else
        {
            status = dsp_crop_and_resize_letterbox(device, &resize_params, nullptr, &letterbox_params);
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
     * Perform DSP Resize
     * This function calls the DSP library to perform resize on a given buffer.
     * DSP will place the result in the output buffer.
     *
     * @param[in] input_buffer_data input buffer data
     * @param[in] output_buffer_data output buffer data
     * @param[in] dsp_interpolation_type interpolation type to use
     * @param[in] use_letterbox should letterbox resize be used
     * @return dsp_status
     */
    dsp_status perform_resize(hailo_buffer_data_t *input_buffer_data, hailo_buffer_data_t *output_buffer_data, dsp_interpolation_type_t dsp_interpolation_type, std::optional<dsp_letterbox_properties_t> letterbox_properties)
    {
        if (device == NULL)
        {
            LOGGER__ERROR("Perform DSP crop and resize ERROR: Device is NULL");
            return DSP_UNINITIALIZED;
        }

        hailo_dsp_buffer_data_t input_dsp_buffer_data = input_buffer_data->As<hailo_dsp_buffer_data_t>();
        hailo_dsp_buffer_data_t output_dsp_buffer_data = output_buffer_data->As<hailo_dsp_buffer_data_t>();

        return perform_resize(&input_dsp_buffer_data.properties, &output_dsp_buffer_data.properties, dsp_interpolation_type, letterbox_properties);
    }

    /**
     * Perform DSP crop and resize
     * This function calls the DSP library to perform crop and resize on a given
     * buffer. DSP will place the result in the output buffer.
     *
     * @param[in] input_buffer_data input buffer data
     * @param[out] output_buffer_data output buffer data
     * @param[in] args crop and resize arguments
     * @param[in] dsp_interpolation_type interpolation type to use
     * @return dsp_status
     */
    dsp_status
    perform_crop_and_resize(hailo_buffer_data_t *input_buffer_data,
                            hailo_buffer_data_t *output_buffer_data,
                            crop_resize_dims_t args,
                            dsp_interpolation_type_t dsp_interpolation_type,
                            std::optional<dsp_letterbox_properties_t> letterbox_properties)
    {
        if (device == NULL)
        {
            LOGGER__ERROR("Perform DSP crop and resize ERROR: Device is NULL");
            return DSP_UNINITIALIZED;
        }

        hailo_dsp_buffer_data_t input_dsp_buffer_data = input_buffer_data->As<hailo_dsp_buffer_data_t>();
        hailo_dsp_buffer_data_t output_dsp_buffer_data = output_buffer_data->As<hailo_dsp_buffer_data_t>();
        return perform_crop_and_resize(&input_dsp_buffer_data.properties, &output_dsp_buffer_data.properties, args, dsp_interpolation_type, letterbox_properties);
    }

    /**
     * Perform multiple crops and resizes on the DSP
     * This function calls the DSP library to perform crops and resizes on a given
     * input buffer. DSP will place the results in the array of output buffer.
     *
     * @param[in] multi_crop_resize_params crop and resize metadata
     * @return dsp_status
     */
    dsp_status
    perform_dsp_multi_resize(dsp_multi_crop_resize_params_t *multi_crop_resize_params)
    {
        return dsp_multi_crop_and_resize(device, multi_crop_resize_params);
    }

    /**
     * Apply a privact mask and perform multiple crops and resizes on the DSP
     * This function calls the DSP library to perform crops and resizes on a given
     * input buffer. DSP will place the results in the array of output buffer.
     *
     * @param[in] multi_crop_resize_params crop and resize metadata
     * @param[in] privacy_mask_params privacy mask metadata
     * @return dsp_status
     */
    dsp_status
    perform_dsp_multi_resize(dsp_multi_crop_resize_params_t *multi_crop_resize_params, dsp_privacy_mask_t *privacy_mask_params)
    {
        return dsp_multi_crop_and_resize_privacy_mask(device, multi_crop_resize_params, privacy_mask_params);
    }

    /**
     * @brief Performs a telescopic multi-resize operation using DSP.
     *
     * This function takes in parameters for multi-crop and resize operations and
     * performs the telescopic multi-resize using the DSP (Digital Signal Processor).
     *
     * @param multi_crop_resize_params A pointer to a structure containing the parameters
     *                                 for the multi-crop and resize operations.
     * @return dsp_status The status of the DSP operation.
     */
    dsp_status
    perform_dsp_telescopic_multi_resize(dsp_multi_crop_resize_params_t *multi_crop_resize_params)
    {
        return dsp_telescopic_multi_crop_and_resize(device, multi_crop_resize_params);
    }

    /**
     * @brief Perform a telescopic multi-resize operation with privacy masking.
     *
     * This function performs a telescopic multi-resize operation on the given
     * multi-crop resize parameters and applies a privacy mask if provided.
     *
     * @param multi_crop_resize_params Pointer to the parameters for multi-crop resize.
     * @param privacy_mask_params Pointer to the parameters for the privacy mask.
     * @return dsp_status Status of the DSP operation.
     */
    dsp_status
    perform_dsp_telescopic_multi_resize(dsp_multi_crop_resize_params_t *multi_crop_resize_params, dsp_privacy_mask_t *privacy_mask_params)
    {
        return dsp_telescopic_multi_crop_and_resize_privacy_mask(device, multi_crop_resize_params, privacy_mask_params);
    }

    dsp_status
    perform_dsp_dewarp(dsp_image_properties_t *input_image_properties,
                       dsp_image_properties_t *output_image_properties,
                       dsp_dewarp_mesh_t *mesh,
                       dsp_interpolation_type_t interpolation,
                       const dsp_isp_vsm_t &isp_vsm,
                       const dsp_vsm_config_t &dsp_vsm_config,
                       const dsp_filter_angle_t &filter_angle,
                       uint16_t *cur_columns_sum,
                       uint16_t *cur_rows_sum,
                       bool do_mesh_correction)

    {
        dsp_dewarp_angular_dis_params_t dewarp_params = {
            .src = input_image_properties,
            .dst = output_image_properties,
            .mesh = mesh,
            .interpolation = interpolation,
            .do_mesh_correction = do_mesh_correction,
            .isp_vsm = isp_vsm,
            .vsm =
                {
                    .config = dsp_vsm_config,
                    .prev_rows_sum = cur_rows_sum,
                    .prev_columns_sum = cur_columns_sum,
                    .cur_rows_sum = cur_rows_sum,
                    .cur_columns_sum = cur_columns_sum,
                },
            .filter_angle = filter_angle,
        };
        return dsp_rot_dis_dewarp(device, &dewarp_params);
    }

    dsp_status
    perform_dsp_dewarp(dsp_image_properties_t *input_image_properties,
                       dsp_image_properties_t *output_image_properties,
                       dsp_dewarp_mesh_t *mesh,
                       dsp_interpolation_type_t interpolation)
    {
        return dsp_dewarp(device, input_image_properties, output_image_properties,
                          mesh, interpolation);
    }

    /**
     * Perform DSP blending using multiple overlays
     * This function calls the DSP library to perform blending between one
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

    dsp_status perform_dsp_dewarp(hailo_buffer_data_t *input_buffer_data,
                                  hailo_buffer_data_t *output_buffer_data,
                                  dsp_dewarp_mesh_t *mesh,
                                  dsp_interpolation_type_t interpolation,
                                  const dsp_isp_vsm_t &isp_vsm,
                                  const dsp_vsm_config_t &dsp_vsm_config,
                                  const dsp_filter_angle_t &filter_angle,
                                  uint16_t *cur_columns_sum,
                                  uint16_t *cur_rows_sum,
                                  bool do_mesh_correction)

    {
        hailo_dsp_buffer_data_t input_dsp_buffer_data = input_buffer_data->As<hailo_dsp_buffer_data_t>();
        hailo_dsp_buffer_data_t output_dsp_buffer_data = output_buffer_data->As<hailo_dsp_buffer_data_t>();
        return perform_dsp_dewarp(&input_dsp_buffer_data.properties, &output_dsp_buffer_data.properties, mesh, interpolation, isp_vsm, dsp_vsm_config, filter_angle, cur_columns_sum, cur_rows_sum, do_mesh_correction);
    }

    dsp_status perform_dsp_dewarp(hailo_buffer_data_t *input_buffer_data,
                                  hailo_buffer_data_t *output_buffer_data,
                                  dsp_dewarp_mesh_t *mesh,
                                  dsp_interpolation_type_t interpolation)
    {
        hailo_dsp_buffer_data_t input_dsp_buffer_data = input_buffer_data->As<hailo_dsp_buffer_data_t>();
        hailo_dsp_buffer_data_t output_dsp_buffer_data = output_buffer_data->As<hailo_dsp_buffer_data_t>();
        return dsp_dewarp(device, &input_dsp_buffer_data.properties, &output_dsp_buffer_data.properties, mesh, interpolation);
    }

    /**
     * Perform DSP blending using multiple overlays
     * This function calls the DSP library to perform blending between one
     * main buffer and multiple overlay buffers.
     * DSP will blend the overlay buffers onto the image frame in place
     *
     * @param[in] image_buffer_data pointer to input image to blend on
     * @param[in] overlay pointer to input images to overlay with
     * @param[in] overlays_count number of overlays to blend
     * @return dsp_status
     */
    dsp_status perform_dsp_multiblend(hailo_buffer_data_t *input_buffer_data,
                                      dsp_overlay_properties_t *overlay,
                                      size_t overlays_count)
    {
        hailo_dsp_buffer_data_t input_dsp_buffer_data = input_buffer_data->As<hailo_dsp_buffer_data_t>();
        return dsp_blend(device, &input_dsp_buffer_data.properties, overlay, overlays_count);
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
        delete[] image_properties->planes;
    }

    hailo_dsp_buffer_data_t hailo_buffer_data_to_dsp_buffer_data(hailo_buffer_data_t *buffer_data)
    {
        return std::move(buffer_data->As<hailo_dsp_buffer_data_t>());
    }

    /**
     * Convert hailo_buffer_data_t to dsp_image_properties_t
     * Allocates memory - caller is responsible for freeing it with free_image_property_planes
     *
     * @param[in] buffer_data pointer to the hailo buffer data
     * @param[out] out_dsp_buffer_props pointer to the dsp output image properties
     * @return dsp_status
     */
    dsp_status hailo_buffer_data_to_dsp_image_props(hailo_buffer_data_t *buffer_data, dsp_image_properties_t *out_dsp_buffer_props)
    {
        hailo_dsp_buffer_data_t dsp_buffer_data = buffer_data->As<hailo_dsp_buffer_data_t>();
        out_dsp_buffer_props->width = dsp_buffer_data.properties.width;
        out_dsp_buffer_props->height = dsp_buffer_data.properties.height;
        out_dsp_buffer_props->format = dsp_buffer_data.properties.format;
        out_dsp_buffer_props->memory = dsp_buffer_data.properties.memory;
        out_dsp_buffer_props->planes_count = dsp_buffer_data.properties.planes_count;

        out_dsp_buffer_props->planes = new dsp_data_plane_t[dsp_buffer_data.properties.planes_count];
        for (size_t i = 0; i < dsp_buffer_data.properties.planes_count; i++)
        {
            out_dsp_buffer_props->planes[i].fd = dsp_buffer_data.planes[i].fd;
            out_dsp_buffer_props->planes[i].bytesperline = dsp_buffer_data.planes[i].bytesperline;
            out_dsp_buffer_props->planes[i].bytesused = dsp_buffer_data.planes[i].bytesused;
        }

        return DSP_SUCCESS;
    }

    /**
     * get_dsp_desired_stride_from_width will return the appropriate buffer stride for each resolution
     * DSP operation with these strides are more efficient
     *
     * @param[in] width the width of the frame
     * @return size_t the desired stride
     */
    size_t get_dsp_desired_stride_from_width(size_t width)
    {
        switch (width)
        {
        case 2160:
            return 2304;
        case 1440:
            return 1536;
        case 1080:
            return 1152;
        case 720:
            return 768;
        case 480:
            return 512;
        case 240:
            return 256;
        default:
            return width;
        }
    }

} // namespace dsp_utils

/** @} */ // end of dsp_utils_definitions