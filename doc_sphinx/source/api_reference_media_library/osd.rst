.. _osd-label:

========================
OSD - On Screen Display
========================

The OSD is an element that empowers users to draw dynamic text, images, and timestamps by leveraging the **DSP capabilities provided in Hailo-15**.
Offloading the task of image blending to the DSP results in high performance overlays.

It uses :code:`osd::Blender` class to blend overlays on incoming frames.

A initial configuration can be specified via JSON. A example of such a JSON can be found at the end of this page.
After the initial configuration, use the API to dynamically add and remove overlays in runtime.

Supported Overlay Types:
========================

The OSD system supports four main types of overlays:

1. **Text Overlays** - Static text with customizable styling
2. **DateTime Overlays** - Auto-updating timestamp displays  
3. **Image Overlays** - Static images from files
4. **Custom Overlays** - User-provided buffers with custom content

Each overlay type has specific parameters and use cases, as detailed in the sections below.

Text Overlays
=============

Text overlays display static text content with extensive customization options.

**Code Example:**

.. code-block:: c++

   osd::TextOverlay text_overlay("text_id", 0.1, 0.3, "Camera Stream", 
       osd::rgba_color_t{255, 255, 255, 255}, // white text
       osd::rgba_color_t{0, 0, 0, 128},       // semi-transparent black background
       24.0,  // font size
       1,     // line thickness  
       1,     // z-index
       "/usr/share/fonts/ttf/LiberationMono-Regular.ttf", // font path
       0,     // angle
       osd::rotation_alignment_policy_t::CENTER, // rotation policy
       osd::rgba_color_t{-1, -1, -1, -1}, // shadow disabled
       0.0, 0.0, // shadow offsets
       osd::font_weight_t::NORMAL, // font weight
       2,     // outline size
       osd::rgba_color_t{0, 0, 0, 255}, // black outline
       osd::HorizontalAlignment::LEFT,
       osd::VerticalAlignment::TOP);
   
   blender->add_overlay(text_overlay);

**Text Overlay Parameters:**

   - **label** - Text content to display
   - **text_color** - RGBA color for text foreground
   - **background_color** - RGBA color for text background
   - **font_path** - Path to TTF font file (default: LiberationMono-Regular.ttf)
   - **font_size** - Font size in points
   - **font_weight** - Either NORMAL or BOLD
   - **line_thickness** - Thickness of text strokes
   - **outline_size** - Size of text outline (0 to disable)
   - **outline_color** - RGBA color for text outline
   - **shadow_color** - RGBA color for text shadow (negative values disable shadow)
   - **shadow_offset_x/y** - Shadow offset relative to frame size

DateTime Overlays
=================

DateTime overlays automatically update to show the current timestamp.

**Code Example:**

.. code-block:: c++

   osd::DateTimeOverlay datetime_overlay("timestamp_id", 0.8, 0.05, 
       "%d-%m-%Y %H:%M:%S", // datetime format (strftime style)
       osd::rgba_color_t{255, 255, 0, 255}, // yellow text
       osd::rgba_color_t{0, 0, 0, 180},     // dark background
       "/usr/share/fonts/ttf/LiberationMono-Regular.ttf",
       18.0, // font size
       1,    // line thickness
       2,    // z-index
       0,    // angle
       osd::rotation_alignment_policy_t::CENTER); // rotation policy
   
   blender->add_overlay(datetime_overlay);

**DateTime Overlay Parameters:**

   - **datetime_format** - strftime format string (default: "%d-%m-%Y %H:%M:%S")
   - All text overlay parameters (color, font, outline, etc.)

.. note::
   DateTime overlays update automatically once per second.

Image Overlays
==============

Image overlays display static images loaded from files.

**Code Example:**

.. code-block:: c++

   osd::ImageOverlay image_overlay("logo_id", 0.85, 0.85, // position (bottom-right)
       0.1, 0.1, // size (10% of frame width/height)
       "/path/to/logo.png", // image file path
       3,  // z-index (on top)
       0,  // no rotation
       osd::rotation_alignment_policy_t::CENTER, // rotation policy
       osd::HorizontalAlignment::RIGHT,
       osd::VerticalAlignment::BOTTOM);
   
   blender->add_overlay(image_overlay);

**Image Overlay Parameters:**

   - **image_path** - Path to image file (PNG, JPEG, etc.)
   - **width** - Image width relative to frame (0.0-1.0)
   - **height** - Image height relative to frame (0.0-1.0)

Custom Overlays
===============

Custom overlays allow users to provide their own buffers with pre-rendered content.

**Code Example:**

.. code-block:: c++

   osd::CustomOverlay custom_overlay("custom_argb", 0.3, 0.5, 0.1, 0.1, 1, 
       osd::custom_overlay_format::ARGB);
   blender->add_overlay(custom_overlay);
   
   // Get the buffer and fill it with custom content
   auto custom_expected = blender->get_overlay("custom_argb");
   auto existing_custom_overlay = std::static_pointer_cast<osd::CustomOverlay>(custom_expected.value());
   HailoMediaLibraryBufferPtr dspbuffer = existing_custom_overlay->get_buffer();

   uint32_t blue = 0xFF0000FF;
   // Fill buffer with blue pixels (ARGB format)
   for (size_t i = 0; i < dspbuffer->get_plane_size(0); i += 4)
   {
      memcpy((int8_t *)(dspbuffer->get_plane_ptr(0)) + i, &blue, sizeof(uint32_t));
   }

   // Enable the overlay to make it visible
   blender->set_overlay_enabled("custom_argb", true);

**Custom Overlay Parameters:**

   - **format** - Buffer format (ARGB or A420)
   - **width/height** - Overlay dimensions relative to frame

.. note::
   Custom overlays are not visible until explicitly enabled via ``set_overlay_enabled()``.

Common Overlay Parameters
=========================

All overlay types share these common parameters:

   - **id** - Unique string identifier for the overlay
   - **x** - Horizontal position relative to frame (0.0-1.0, left to right)
   - **y** - Vertical position relative to frame (0.0-1.0, top to bottom)
   - **z_index** - Layering order (higher values appear on top)
   - **angle** - Rotation angle in degrees
   - **rotation_policy** - Rotation center (CENTER or TOP_LEFT)
   - **horizontal_alignment** - LEFT, CENTER, or RIGHT alignment
   - **vertical_alignment** - TOP, CENTER, or BOTTOM alignment

.. warning::
   Setting rotation on DateTime overlays may cause performance issues.

Blender API Reference
=====================

The ``osd::Blender`` class provides the main interface for managing overlays:

**Creation:**

.. code-block:: c++

   // Create a blender instance
   auto blender_result = osd::Blender::create();
   if (blender_result.has_value()) {
       auto blender = blender_result.value();
   }
   
   // Create with JSON configuration
   auto blender_with_config = osd::Blender::create(json_config_string);

**Adding Overlays:**

.. code-block:: c++

   // Synchronous
   media_library_return result = blender->add_overlay(overlay);
   
   // Asynchronous
   std::shared_future<media_library_return> future_result = blender->add_overlay_async(overlay);

**Managing Overlays:**

.. code-block:: c++

   // Get overlay information
   auto overlay_result = blender->get_overlay("overlay_id");
   if (overlay_result.has_value()) {
       auto overlay = overlay_result.value();
       // Cast to specific type if needed
       auto text_overlay = std::static_pointer_cast<osd::TextOverlay>(overlay);
   }
   
   // Update overlay
   blender->set_overlay(modified_overlay);
   
   // Enable/disable overlay
   blender->set_overlay_enabled("overlay_id", true);
   
   // Remove overlay
   blender->remove_overlay("overlay_id");

**Frame Processing:**

.. code-block:: c++

   // Set frame dimensions (call once when frame size is known)
   blender->set_frame_size(frame_width, frame_height);
   
   // Blend overlays onto frame
   media_library_return result = blender->blend(buffer);

Alignment and Positioning
=========================

Overlay positioning uses a relative coordinate system where (0,0) is the top-left corner and (1,1) is the bottom-right corner.

**Horizontal Alignment:**
   - ``HorizontalAlignment::LEFT`` - Align to left edge of position
   - ``HorizontalAlignment::CENTER`` - Center horizontally on position  
   - ``HorizontalAlignment::RIGHT`` - Align to right edge of position

**Vertical Alignment:**
   - ``VerticalAlignment::TOP`` - Align to top edge of position
   - ``VerticalAlignment::CENTER`` - Center vertically on position
   - ``VerticalAlignment::BOTTOM`` - Align to bottom edge of position

**Example:**

.. code-block:: c++

   // Position overlay at bottom-right corner of frame
   osd::TextOverlay corner_text("corner", 1.0, 1.0, "Bottom Right",
       text_color, bg_color, font_size, thickness, z_index, font_path, 0,
       osd::rotation_alignment_policy_t::CENTER, shadow_color, 0, 0, osd::font_weight_t::NORMAL, 0, outline_color,
       osd::HorizontalAlignment::RIGHT,  // Align right edge to x position
       osd::VerticalAlignment::BOTTOM);  // Align bottom edge to y position

Color Formats
=============

Colors use RGBA format with values 0-255:

.. code-block:: c++

   // Fully opaque colors
   osd::rgba_color_t red = {255, 0, 0, 255};
   osd::rgba_color_t green = {0, 255, 0, 255};
   osd::rgba_color_t blue = {0, 0, 255, 255};
   
   // Semi-transparent colors
   osd::rgba_color_t semi_black = {0, 0, 0, 128}; // 50% transparency
   
   // Disabled features (negative alpha)
   osd::rgba_color_t disabled = {-1, -1, -1, -1}; // Disables shadow/outline

Performance Considerations
==========================

- **DateTime Rotation**: Avoid rotating DateTime overlays for better performance
- **Z-Index**: Use minimal z-index values; higher values require more blending operations
- **Custom Overlays**: Pre-render content when possible rather than updating every frame
- **Overlay Count**: Limit the number of simultaneous overlays for optimal performance
- **Font Caching**: Reuse font paths across overlays to benefit from internal caching

More examples can be found in:

.. code-block:: sh

   hailo-media-library/api/examples/frontend_example.cpp

Error Handling
==============

The OSD API uses the ``tl::expected`` pattern for error handling:

.. code-block:: c++

   auto blender_result = osd::Blender::create();
   if (!blender_result.has_value()) {
       media_library_return error = blender_result.error();
       // Handle error
       return;
   }
   auto blender = blender_result.value();
   
   // For operations returning media_library_return
   media_library_return result = blender->add_overlay(overlay);
   if (result != MEDIA_LIBRARY_SUCCESS) {
       // Handle error
   }

Common Error Scenarios:
-----------------------

- **Invalid overlay ID**: Using duplicate IDs or invalid characters
- **Invalid coordinates**: Position/size values outside [0,1] range  
- **Missing files**: Font or image files not found
- **Invalid formats**: Unsupported image formats or invalid color values
- **Memory allocation**: Insufficient memory for overlay buffers

.. doxygenfile:: osd.hpp
   :project: media_library

JSON Examples
-------------
For more explanations on the JSON parameters, see the :ref:`Encoder + OSD Configurations <overview-configurations-label>`.

**Hailo Encoder:**

.. literalinclude:: ../../../hailo-media-library/api/examples/config_examples/encoder_config_example.json
   :language: json

**JPEG Encoder:**

.. literalinclude:: ../../../hailo-media-library/api/examples/config_examples/jpeg_encoder_config_example.json
   :language: json

