===============
Pipeline Viewer
===============

Overview
--------

.. figure:: /_images_src/hailo_analytics/debug_and_visualization/single_stream_app.png
   :alt: Simple Dot Graph
   :align: center
   :width: 60%

   A simple pipeline DOT graph

Hailo AI Analytics provides built-in support for exporting pipeline graphs to DOT format, which can be visualized using various GraphViz-compatible tools. This is extremely useful for:

* Understanding pipeline topology and data flow
* Debugging connection issues between stages
* Documenting pipeline architecture
* Verifying that your pipeline is structured as intended

Enabling DOT Export
-------------------

To automatically export your pipeline graph to a DOT file, set the following environment variable before running your application:

.. code-block:: bash

   export HAILO_ANALYTICS_DUMP_DOT=1

By default, the DOT file will be saved to ``/tmp/<pipeline_name>.dot``, where ``<pipeline_name>`` is the name you provided when building the pipeline.

Customizing Output Directory
-----------------------------

To specify a custom output directory for the DOT files, set the ``HAILO_ANALYTICS_DUMP_DOT_DIR`` environment variable:

.. code-block:: bash

   export HAILO_ANALYTICS_DUMP_DOT=1
   export HAILO_ANALYTICS_DUMP_DOT_DIR=/path/to/output/directory

The pipeline graph will be saved as ``/path/to/output/directory/<pipeline_name>.dot``.

Example Usage
-------------

.. code-block:: bash

   # Enable DOT export with custom directory
   export HAILO_ANALYTICS_DUMP_DOT=1
   export HAILO_ANALYTICS_DUMP_DOT_DIR=$HOME/pipeline_graphs
   
   # Run your application
   ./my_hailo_app

   # The pipeline graph will be saved to ~/pipeline_graphs/<pipeline_name>.dot

Visualizing DOT Files
---------------------

Once you have generated a DOT file, you can visualize it using several methods:

Interactive Viewing with xdot
------------------------------

``xdot`` is an interactive viewer for DOT files that allows panning, zooming, and exploring the graph:

.. code-block:: bash

   # Install xdot (if not already installed)
   sudo apt-get install xdot
   
   # View the DOT file interactively
   xdot /tmp/my_pipeline.dot

Converting to PNG
-----------------

You can convert the DOT file to a PNG image for easy sharing and documentation:

.. code-block:: bash

   # Install GraphViz (if not already installed)
   sudo apt-get install graphviz
   
   # Convert to PNG
   dot -Tpng /tmp/my_pipeline.dot -o pipeline_graph.png
   
   # View the PNG
   eog pipeline_graph.png  # or any image viewer

Troubleshooting
---------------

DOT File Not Generated
-----------------------

If the DOT file is not being generated:

1. Verify that ``HAILO_ANALYTICS_DUMP_DOT=1`` is set in your environment
2. Check that the output directory exists and is writable
3. Look for error messages in the application logs

Permission Denied Errors
-------------------------

If you get permission denied errors:

.. code-block:: bash

   # Ensure the output directory is writable
   mkdir -p $HAILO_ANALYTICS_DUMP_DOT_DIR
   chmod 755 $HAILO_ANALYTICS_DUMP_DOT_DIR