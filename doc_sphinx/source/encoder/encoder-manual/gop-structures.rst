GOP Structure Configuration
===========================

The GOP structure is represented by a table, which is given as an input
parameter to configure the encoder. The following two simple examples
show how to define a GOP structure table.

GOP Structure of Size 4
-----------------------

Consider the GOP structure of size 4 shown in the figures below.

.. _fig_gop_structure_size_4:

.. figure:: /_images_src/gop-structure4.jpg
   :alt: GOP Structure of Size 4
   :align: center

   GOP Structure of Size 4

The four frames within a GOP are described below in decoding order.

-  Frame 1 is a P frame with POC 4, referencing one frame with POC 0.
   The reference frame is defined by delta POC value relative to current
   frame. In this case it is -4.

-  Frame 2 is a B frame with POC 2, referencing two frames with POC 0
   and 4 respectively. So its reference frames are listed as -2 and 2.

-  Frame 3 is a B frame with POC 1, referencing two frames with POC 0
   and 2 respectively. It also needs to keep the frame with POC 4 as a
   reference frame to be used in future. So its reference frame list is
   -1, 1 and 3.

-  Frame 4 is a B frame with POC 3, referencing two frames with POC 2
   and 4 respectively. Its reference list is -1 and 1.

The corresponding GOP structure table is shown below, where:

-  **QP Offset** will be added to the QP parameter to set the final QP;

-  **QP Factor** will be used in rate distortion optimization. Higher
   values mean lower quality and less bits. Typical range is between 0.3
   and 1;

-  **Used by Current Frame** specifies whether each reference frame in
   **Reference List** is used in current (1) or future (0).

.. _table_gop_strucure_of_size_4:

.. csv-table:: GOP Structure Table of Size 4
   :file: table_gop_strucure_of_size_4.csv

GOP Structure of Size 1
-----------------------

The GOP structure in the figure below only contains one P frame which
references only its previous frame, so the reference list is simply -1.

.. _fig_gop_structure_size_1:

.. figure:: /_images_src/gop-structure1.jpg
   :alt: GOP Structure of Size 1
   :align: center

   GOP Structure of Size 1

The GOP structure table is shown in the following table.

.. _table_gop_strucure_table_of_size_1:

.. csv-table:: GOP Structure Table of Size 1
   :file: table_gop_strucure_table_of_size_1.csv

All GOP structure tables that will be used in the sequence need to be
saved in the input parameter encIn.gopConfigs to configure encoder. Use
the :ref:`VCEncGopConfig <VCEncGopConfig>` data structure within the
:ref:`VCEncIn <VCEncIn>` data structure to configure the parameters in the
GOP structure.

GOP Structure of Size 1 with Long Term Reference
------------------------------------------------

The GOP structure only contains one B frame which references only its
previous frame, so the reference list is simply -1. In addition, it
specifies longTermRefPicRate=50 to enable long term reference, which
updates every 50 frames. So the total number of references is 2, and the
type is B frame. The GOP structure table is shown in Table 10.

.. _table_gop_strucure_table_of_size_1_long_term:

.. csv-table:: GOP Structure Table of Size 1 with Long Term Reference
   :file: table_gop_strucure_table_of_size_1_long_term.csv

GOP Structure of Size 4 with 3 Temporal Layers
----------------------------------------------

The GOP structure defines a 3 layer pattern:

.. _fig_gop_strucure_size_4_3_layers:

.. figure:: /_images_src/gop_strucure_size_4_3_layers.png
   :alt: GOP Structure of Size 4 with 3 Temporal Layers
   :align: center

   GOP Structure of Size 4 with 3 Temporal Layers

The GOP structure table is shown in the following table.

.. _table_gop_strucure_table_of_size_4_with_3_layers:

.. csv-table:: GOP Structure Table of Size 4 with 3 Layers
   :file: table_gop_strucure_table_of_size_4_with_3_layers.csv
