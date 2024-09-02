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

#include "media_library/media_library_logger.hpp"
#include "text_overlay_impl.hpp"
#include <opencv2/core.hpp>
#include <opencv2/core/utils/filesystem.hpp>

#include <freetype/ftbbox.h>
#include <freetype/ftimage.h>
#include <freetype/ftoutln.h>
#include <freetype/ftsynth.h>

#define INT_FROM_26_6_ROUND(x) (static_cast<int>(((x) + ((x) < 0 ? -(1 << 5) : (1 << 5))) >> 6))

#define INT_FROM_26_6_NO_ROUND(x) (static_cast<int>((x) >> 6))

#define INT_TO_26_6(x) (static_cast<FT_F26Dot6>((x) << 6))

#define INT_TO_16_16(x) (static_cast<FT_Fixed>((x) << 16))

#define PADDING (3)

SimpleTextOverlayImpl::SimpleTextOverlayImpl(const osd::BaseTextOverlay &overlay,
                                             cv::Size2f extra_size,
                                             cv::Point2f text_position,
                                             media_library_return &status)
    : OverlayImpl(overlay.id, overlay.x, overlay.y, 0, 0, overlay.z_index, overlay.angle,
                  overlay.rotation_alignment_policy, true, overlay.horizontal_alignment, overlay.vertical_alignment),
      m_label(""),
      m_rgba_text_color{(double)overlay.text_color.red, (double)overlay.text_color.green, (double)overlay.text_color.blue, (double)overlay.text_color.alpha},
      m_rgba_outline_color{(double)overlay.outline_color.red, (double)overlay.outline_color.green, (double)overlay.outline_color.blue, (double)overlay.outline_color.alpha},
      m_font_path(overlay.font_path), m_font_size(overlay.font_size), m_outline_size(overlay.outline_size), m_font_weight(overlay.font_weight),
      m_extra_size(extra_size), m_text_position(text_position),
      m_hb_font(nullptr, hb_font_destroy), m_hb_buffer(nullptr, hb_buffer_destroy), m_ft_library(nullptr, FT_Done_FreeType), m_ft_face(nullptr, FT_Done_Face), m_ft_stroker(nullptr, FT_Stroker_Done)
{
    if (!cv::utils::fs::exists(m_font_path))
    {
        LOGGER__ERROR("Error: file {} does not exist", m_font_path);
        status = MEDIA_LIBRARY_INVALID_ARGUMENT;
        return;
    }

    FT_Library library;
    FT_Error ret = FT_Init_FreeType(&library);
    if (ret)
    {
        LOGGER__ERROR("Error: FT_Init_FreeType() failed with {}", ret);
        status = MEDIA_LIBRARY_FREETYPE_ERROR;
        return;
    }
    m_ft_library.reset(library);

    FT_Face face;
    ret = FT_New_Face(m_ft_library.get(), m_font_path.c_str(), 0, &face);
    if (ret)
    {
        LOGGER__ERROR("Error: FT_New_Face() failed with {}", ret);
        status = MEDIA_LIBRARY_FREETYPE_ERROR;
        return;
    }
    m_ft_face.reset(face);

    m_hb_font.reset(hb_ft_font_create(m_ft_face.get(), nullptr));
    if (m_hb_font.get() == nullptr)
    {
        LOGGER__ERROR("Error: hb_ft_font_create() failed");
        status = MEDIA_LIBRARY_FREETYPE_ERROR;
        return;
    }

    m_hb_buffer.reset(hb_buffer_create());
    if (m_hb_buffer.get() == nullptr)
    {
        LOGGER__ERROR("Error: hb_buffer_create() failed");
        status = MEDIA_LIBRARY_FREETYPE_ERROR;
    }

    if (m_outline_size > 0)
    {
        FT_Stroker stroker;
        ret = FT_Stroker_New(m_ft_library.get(), &stroker);
        if (ret)
        {
            LOGGER__ERROR("Error: FT_Stroker_New() failed with {}", ret);
            status = MEDIA_LIBRARY_FREETYPE_ERROR;
            return;
        }
        m_ft_stroker.reset(stroker);
        
        FT_Stroker_Set(m_ft_stroker.get(), INT_TO_26_6(m_outline_size), FT_STROKER_LINECAP_ROUND, FT_STROKER_LINEJOIN_ROUND, 0);
    }

    status = MEDIA_LIBRARY_SUCCESS;
}

tl::expected<SimpleTextOverlayImplPtr, media_library_return> SimpleTextOverlayImpl::create(const osd::BaseTextOverlay &overlay, cv::Size2f extra_size, cv::Point2f text_position)
{
    media_library_return status = MEDIA_LIBRARY_UNINITIALIZED;
    auto osd_overlay = std::make_shared<SimpleTextOverlayImpl>(overlay, extra_size, text_position, status);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return osd_overlay;
}

std::shared_ptr<osd::Overlay> SimpleTextOverlayImpl::get_metadata()
{
    // This is an internal class, so we don't need to return metadata
    return nullptr;
}

tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> SimpleTextOverlayImpl::create_dsp_overlays(int frame_width, int frame_height)
{
    // If label is empty, display nothing
    if (m_label == "")
    {
        return std::vector<dsp_overlay_properties_t>();
    }

    set_enabled(false);
    auto status = create_text_m_mat(frame_width, frame_height);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return OverlayImpl::create_dsp_overlays(frame_width, frame_height);
}

// this function was refactored from OpenCV's freetype.cpp: getTextSize()
tl::expected<size_baseline, media_library_return> SimpleTextOverlayImpl::get_text_size_baseline(int frame_width, int frame_height)
{
    FT_Error ret = FT_Set_Pixel_Sizes(m_ft_face.get(), m_font_size, m_font_size);
    if (ret)
    {
        LOGGER__ERROR("Error: FT_Set_Pixel_Sizes() failed with {}", ret);
        return tl::make_unexpected(MEDIA_LIBRARY_FREETYPE_ERROR);
    }

    m_hb_buffer.reset(hb_buffer_create());
    if (m_hb_buffer.get() == nullptr)
    {
        LOGGER__ERROR("Error: hb_buffer_create() failed");
        return tl::make_unexpected(MEDIA_LIBRARY_FREETYPE_ERROR);
    }

    FT_Vector currentPos = {0, 0};

    hb_buffer_add_utf8(m_hb_buffer.get(), m_label.c_str(), -1, 0, -1); // -1 for null-treminated text
    hb_buffer_guess_segment_properties(m_hb_buffer.get());
    hb_shape(m_hb_font.get(), m_hb_buffer.get(), NULL, 0);

    unsigned int glyph_count;
    hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(m_hb_buffer.get(), &glyph_count);
    if (!glyph_info)
    {
        LOGGER__ERROR("Error: hb_buffer_get_glyph_infos() failed");
        return tl::make_unexpected(MEDIA_LIBRARY_FREETYPE_ERROR);
    }

    // Initilize BoundaryBox ( in OpenCV coordinates )
    int xMin = INT_MAX, yMin = INT_MAX;
    int xMax = INT_MIN, yMax = INT_MIN;

    for (unsigned int i = 0; i < glyph_count; i++)
    {
        ret = FT_Load_Glyph(m_ft_face.get(), glyph_info[i].codepoint, 0);
        if (ret)
        {
            LOGGER__ERROR("Error: FT_Load_Glyph() failed with {}", ret);
            return tl::make_unexpected(MEDIA_LIBRARY_FREETYPE_ERROR);
        }

        if (m_font_weight == osd::font_weight_t::BOLD)
        {
            FT_GlyphSlot_Embolden(m_ft_face->glyph);
        }
        
        FT_Outline outline = m_ft_face->glyph->outline;

        // Flip ( in FreeType coordinates )
        FT_Matrix mtx = { INT_TO_16_16(1), INT_TO_16_16(0), INT_TO_16_16(0), -INT_TO_16_16(1) };
        FT_Outline_Transform(&outline, &mtx);

        // Move to current position ( in FreeType coordinates )
        FT_Outline_Translate(&outline, currentPos.x, currentPos.y);

        // Get BoundaryBox ( in FreeType coordinatrs )
        FT_BBox bbox;
        ret = FT_Outline_Get_BBox(&outline, &bbox);
        if (ret)
        {
            LOGGER__ERROR("Error: FT_Outline_Get_BBox() failed with {}", ret);
            return tl::make_unexpected(MEDIA_LIBRARY_FREETYPE_ERROR);
        }

        // If codepoint is space(0x20), it has no glyph.
        // A dummy boundary box is needed when last code is space.
        if ((bbox.xMin == 0) && (bbox.xMax == 0) && (bbox.yMin == 0) && (bbox.yMax == 0))
        {
            bbox.xMin = currentPos.x;
            bbox.xMax = currentPos.x + m_ft_face->glyph->advance.x;
            bbox.yMin = yMin;
            bbox.yMax = yMax;
        }

        // Add outline to bbox
        bbox.xMax += INT_TO_26_6(m_outline_size * 2);
        bbox.yMax += INT_TO_26_6(m_outline_size * 2);

        // Update current position ( in FreeType coordinates )
        currentPos.x += m_ft_face->glyph->advance.x + INT_TO_26_6(m_outline_size);
        currentPos.y += m_ft_face->glyph->advance.y;

        // Update BoundaryBox ( in OpenCV coordinates )
        xMin = cv::min(xMin, INT_FROM_26_6_ROUND(bbox.xMin));
        xMax = cv::max(xMax, INT_FROM_26_6_ROUND(bbox.xMax));
        yMin = cv::min(yMin, INT_FROM_26_6_ROUND(bbox.yMin));
        yMax = cv::max(yMax, INT_FROM_26_6_ROUND(bbox.yMax));
    }

    // Calculate width/height/baseline (in OpenCV coordinates)
    int width = xMax - xMin;
    int height = -yMin;

    // Calculate right bearing and increase width so text isn't cut from the right
    FT_Pos right_bearing = m_ft_face->glyph->advance.x - (m_ft_face->glyph->metrics.horiBearingX + m_ft_face->glyph->metrics.width);
    width += INT_FROM_26_6_NO_ROUND(right_bearing);

    // Add outline once more to make right size look good
    width += m_outline_size;

    int baseline = yMax;
    cv::Size base_size = cv::Size(width, height);

    // account for extra shadow size
    cv::Size extra_size = cv::Size(m_extra_size.width * frame_width, m_extra_size.height * frame_height);
    cv::Size final_size = base_size + extra_size;

    // Round up to even numbers to support YUV420
    final_size.width += final_size.width % 2;
    final_size.height += final_size.height % 2;
    baseline += baseline % 2;

    // account for baseline
    final_size.height += baseline;

    return std::make_pair(final_size, baseline);
}

void SimpleTextOverlayImpl::put_glyph(cv::Mat dst, FT_Bitmap *bmp, cv::Point glyph_position, cv::Scalar color)
{
    // find the pixels in the destination Matrix corresponding to the glyph and color them
    for (int row = 0; row < (int)bmp->rows; row++)
    {
        // skip pixels outside top edge
        if (glyph_position.y + row < 0)
        {
            continue;
        }
        
        // finish if we've passed bottom edge
        if (glyph_position.y + row >= dst.rows)
        {
            break;
        }

        for (int col = 0; col < bmp->pitch; col++)
        {
            int cl = bmp->buffer[row * bmp->pitch + col];
            if (cl == 0)
            {
                continue;
            }
            for (int bit = 7; bit >= 0; bit--)
            {
                // skip pixels outside left edge
                if (glyph_position.x + col * 8 + (7 - bit) < 0)
                {
                    continue;
                }

                // finish if we've passed right edge
                if (glyph_position.x + col * 8 + (7 - bit) >= dst.cols)
                {
                    break;
                }

                if (((cl >> bit) & 0x01) == 1)
                {
                    cv::Vec4b *ptr = dst.ptr<cv::Vec4b>(glyph_position.y + row, glyph_position.x + col * 8 + (7 - bit));

                    // color is rgba, OpenCV matrix is bgra
                    (*ptr)[0] = color[2];
                    (*ptr)[1] = color[1];
                    (*ptr)[2] = color[0];
                    (*ptr)[3] = color[3];
                }
            }
        }
    }
}

// this function was refactored from OpenCV's freetype.cpp: putTextBitmapMono()
media_library_return SimpleTextOverlayImpl::put_text(cv::Mat dst, cv::Point org)
{
    auto ft_done_glyph = [](FT_Glyph *glyph) { FT_Done_Glyph(*glyph); };
    using ft_glyph_ptr = std::unique_ptr<FT_Glyph, decltype(ft_done_glyph)>;

    unsigned int glyph_count;
    hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(m_hb_buffer.get(), &glyph_count);
    if (!glyph_info)
    {
        LOGGER__ERROR("Error: hb_buffer_get_glyph_infos() failed");
        return MEDIA_LIBRARY_FREETYPE_ERROR;
    }

    for (unsigned int i = 0; i < glyph_count; i++)
    {
        FT_Error ret = FT_Load_Glyph(m_ft_face.get(), glyph_info[i].codepoint, 0);
        if (ret)
        {
            LOGGER__ERROR("Error: FT_Load_Glyph() failed with {}", ret);
            return MEDIA_LIBRARY_FREETYPE_ERROR;
        }

        if (m_font_weight == osd::font_weight_t::BOLD)
        {
            FT_GlyphSlot_Embolden(m_ft_face->glyph);
        }

        cv::Point glyph_position = org;            
        glyph_position.y -= INT_FROM_26_6_NO_ROUND(m_ft_face->glyph->metrics.horiBearingY);
        glyph_position.x += INT_FROM_26_6_NO_ROUND(m_ft_face->glyph->metrics.horiBearingX);

        // Render outline if needed
        if (m_outline_size > 0 && m_rgba_text_color != m_rgba_outline_color)
        {
            ft_glyph_ptr ft_glyph(nullptr, ft_done_glyph);
            FT_Glyph glyph;
            ret = FT_Get_Glyph(m_ft_face->glyph, &glyph);
            if (ret)
            {
                LOGGER__ERROR("Error: FT_Get_Glyph() failed with {}", ret);
                return MEDIA_LIBRARY_FREETYPE_ERROR;
            }
            ft_glyph.reset(&glyph);

            ret = FT_Glyph_StrokeBorder(ft_glyph.get(), m_ft_stroker.get(), false, true);
            if (ret)
            {
                LOGGER__ERROR("Error: FT_Glyph_StrokeBorder() failed with {}", ret);
                return MEDIA_LIBRARY_FREETYPE_ERROR;
            }

            ret = FT_Glyph_To_Bitmap(ft_glyph.get(), FT_RENDER_MODE_MONO, 0, true);
            if (ret)
            {
                LOGGER__ERROR("Error: FT_Glyph_To_Bitmap() failed with {}", ret);
                return MEDIA_LIBRARY_FREETYPE_ERROR;
            }

            FT_BitmapGlyph bitmap_glyph = reinterpret_cast<FT_BitmapGlyph>(*ft_glyph.get());
            put_glyph(dst, &bitmap_glyph->bitmap, glyph_position, m_rgba_outline_color);
        }

        // Render glyph (inside the outline, if needed)
        ret = FT_Render_Glyph(m_ft_face->glyph, FT_RENDER_MODE_MONO);
        if (ret)
        {
            LOGGER__ERROR("Error: FT_Render_Glyph() failed with {}", ret);
            return MEDIA_LIBRARY_FREETYPE_ERROR;
        }

        glyph_position.y += m_outline_size;
        glyph_position.x += m_outline_size;
        put_glyph(dst, &m_ft_face->glyph->bitmap, glyph_position, m_rgba_text_color);

        // advance origin point (the integer part) by glyph size
        org.x += INT_FROM_26_6_NO_ROUND(m_ft_face->glyph->advance.x) + m_outline_size;
        org.y += INT_FROM_26_6_NO_ROUND(m_ft_face->glyph->advance.y);
    }

    hb_buffer_reset(m_hb_buffer.get());

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return SimpleTextOverlayImpl::create_text_m_mat(int frame_width, int frame_height)
{
    auto text_size_baseline_expected = get_text_size_baseline(frame_width, frame_height);
    if (!text_size_baseline_expected)
    {
        LOGGER__ERROR("Error: get_text_size_baseline() failed with {}", text_size_baseline_expected.error());
        return text_size_baseline_expected.error();
    }
    cv::Size text_size = text_size_baseline_expected.value().first;
    int baseline = text_size_baseline_expected.value().second;

    /* Calculate text position by adjusting according to m_text_position */
    auto text_position_offset = cv::Point(m_text_position.x * frame_width, m_text_position.y * frame_height);
    auto text_position = cv::Point(0, text_size.height - baseline - m_extra_size.height * frame_height) + text_position_offset;

    // init the matrix with transparent background (alpha channel = 0)
    m_image_mat = cv::Mat(text_size, CV_8UC4, cv::Scalar{-1, -1, -1, 0});

    // render the text onto the image matrix (alpha channel will be set for pixels with text)
    auto status = put_text(m_image_mat, text_position);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Error: put_text() failed with {}", status);
        return status;
    }

    return MEDIA_LIBRARY_SUCCESS;
}

/* Text will be rendered with the new label only after calling create_dsp_overlays */
void SimpleTextOverlayImpl::change_text(const std::string &label)
{
    m_label = label;
}

cv::Size SimpleTextOverlayImpl::get_text_size() const
{
    return m_image_mat.size();
}