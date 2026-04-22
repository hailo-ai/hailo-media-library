Video Frame Storage Format
==========================

Input picture formats supported by the encoder are:

1. planar YCbCr 4:2:0,

2. semiplanar YCbCr 4:2:0,

3. interleaved YCbCr 4:2:2,

4. 32-bit RGB,

5. 16-bit RGB,

6. planar YCbCr 4:2:0 10 bits (I010),

7. semiplanar YCbCr 4:2:0 10 bits (P010),

8. YCbCr 4:2:0 10 bits (Y0L2) and

9. planar YCbCr 4:2:0 10 bits packed.

The table on the following page shows how the supported input picture
formats are stored in external memory.

A planar YCbCr 4:2:0 10 bits (I010) and a planar YCbCr 4:2:0 10 bits
packed picture are stored in external memory like a planar YCbCr 4:2:0
picture.

A semiplanar YCbCr 4:2:0 10 bits (P010) picture is stored in external
memory like a semiplanar YCbCr 4:2:0 picture.

YCbCr 4:2:0 10 bits (Y0L2) picture are stored in external memory like a
YCbCr 4:2:2 picture. The pixels are stored in 64-bit chunks containing
2x2 luma values and a set of chroma values (and optional punch-through
alpha per pixel). An alpha-value of 0 means the corresponding pixel is
completely transparent while an alpha value of 1 means the corresponding
pixel is completely opaque. The luma values in the 2x2 chunks are
numbered from top to bottom, left to right. An example showing the
addressing of a 16x4 pixel image is defined in the
:ref:`table below <table_addressing_16x4_yol2>`.

.. _table_storage_external_memory:

.. list-table:: Storage of Input Picture Formats in External Memory
   :header-rows: 0

   * - **Planar YCbCr 4:2:0**: The luminance and both chrominance components are stored in
       three buffers and they must be located in a linear and physically contiguous memory block.
     - .. image:: /_images_src/planar_YCbCr420.png
   * - **Semiplanar YCbCr 4:2:0**: The luminance is equal to planar format but the Cb and Cr
       chrominance components are interleaved together. Separate formats are specified for different
       orders of Cb and Cr.
     - .. image:: /_images_src/semiplanar_YCbCr420.png
   * - **Interleaved YCbCr 4:2:2**: The sample order is selected at initialization to be either
       Y-Cb-Y-Cr or Cb-Y-Cr-Y.
     - .. image:: /_images_src/interleaved_YCbCr422.png
   * - **RGB**: The sample order and bit-depth of color components are selected at initialization
       to be 16-bit [#f_16bit_formats]_ or 32-bit [#f_32bit_formats]_ RGB formats.
     - .. image:: /_images_src/rgb.png


.. [#f_16bit_formats] 16-bit RGB formats: RGB565, BGR565, RGB555, BGR555, RGB444 or BGR444.
   For example, RGB565 corresponds to a 16-bit word containing 5 MSB bits for Red-component,
   6 bits for Green-component and 5 LSB bits for Blue-component.

.. [#f_32bit_formats] 32-bit RGB formats: RGB888, BGR888, RGB101010, BGR101010

Planar and semiplanar YCbCr 4:2:0 pixel formats are defined in the table
below.

.. _table_pixel_formats:

.. image:: /_images_src/table_pixel_formats.png

.. _table_addressing_16x4_yol2:

.. csv-table:: Addressing of a 16x4 pixel image in Y0L2 Format
   :file: table_addressing_16x4_yol2.csv

The input picture data endianness is set when the encoder software is
compiled and can’t be changed during run-time. The sizes of the
different components are based on the picture’s dimensions and the input
format as defined in the table below.

(where: W = width, H = height)

.. _table_component_sizes:

.. csv-table:: Component Sizes of Input Formats
   :file: table_component_sizes.csv

Frame Size Limitations
----------------------

Input picture formats have limitations based on the input picture size
and the encoded picture size.

.. list-table::
   :widths: auto
   :header-rows: 1

   * - | Input Format
     - | Limitations [#f1]_ [#f2]_
   * - | YCbCr 4:2:0
       | YCbCr 4:2:2
       | RGB
       | Planar YCbCr 4:2:0 10 bits (I010)
       | Semiplanar YCbCr 4:2:0 10 bits (P010)
     - | encoded_width % 2 = 0
       | encoded_height % 2 = 0
       | input_width >= (encoded_width + 7) & (~7)
       | input_width % 16 = 0
       | input_height >= encoded_height
       | input_height % 2 = 0
   * - | Planar YCbCr 4:2:0 10 bits packed
     - | encoded_width % 2 = 0
       | encoded_height % 2 = 0
       | input_width >= (encoded_width + 7) & (~7)
       | input_width % 64 = 0
       | input_height >= encoded_height
       | input_height % 2 = 0
   * - | YCbCr 4:2:0 10 bits (Y0L2)
     - | encoded_width % 2 = 0
       | encoded_height % 2 = 0
       | input_width >= (encoded_width + 3) & (~3)
       | input_width % 4 = 0
       | input_height >= encoded_height
       | input_height % 2 = 0

.. [#f1] % - remainder operator
.. [#f2] & - bitwise AND operator

