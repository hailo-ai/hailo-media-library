================
Available Stages
================

A variety of pre-defined stages are available in Hailo AI Analytics to facilitate common processing tasks.
These stages can be easily integrated into your analytics pipelines to perform functions such as AI inference, video encoding, and network transmission.

Ai Stages
=========

.. toctree::
   :maxdepth: 1

   ai_stages/ai_stage
   ai_stages/postprocess_stage
   ai_stages/analytics_db_stage
   ai_stages/lightweight_tracker_stage

Overlay Stages
==============

.. toctree::
   :maxdepth: 1

   overlay_stages/overlay_stage

Codec Stages
============

.. toctree::
   :maxdepth: 1

   codec_stages/encoder_stage
   codec_stages/analytic_metadata_packager_stage

Cropping Stages
===============

.. toctree::
   :maxdepth: 1

   cropping_stages/dsp_cropping
   cropping_stages/bbox_crop_stage
   cropping_stages/tiling_crop_stage
   cropping_stages/aggregator_stage
   cropping_stages/sync_aggregator_stage

Muxing Stages
=============

.. toctree::
   :maxdepth: 1

   muxing_stages/muxer_stage
   muxing_stages/demuxer_stage
   muxing_stages/bundle_streams_stage
   muxing_stages/split_streams_stage

Routing Stages
==============

.. toctree::
   :maxdepth: 1

   routing_stages/callback_stage
   routing_stages/freeze_stage
   routing_stages/tee_stage
   routing_stages/tracker_traffic_ctrl_stage
   routing_stages/valve_stage

Sink Stages
===========

.. toctree::
   :maxdepth: 1

   sink_stages/app_sink_stage
   sink_stages/rtp_converter_stage
   sink_stages/udp_stage
   sink_stages/zmq_comm_stage

Source Stages
=============

.. toctree::
   :maxdepth: 1

   source_stages/frontend_stage