===========================
Digital Image Stabilization
===========================

Digital Image Stabilization (DIS) aims to stabilize the vibrations in input frames, due to camera shakes. It does so by cropping from each frame a part which it views from the same part of the scene. DIS stabilizes the frame based on the instantaneous measurement of the Frame Motion Vector (FMV). The crop area is determined by the output (stabilized) Field of View (FOV), which must be smaller than the actual FOV of the camera. The difference forms the room for stabilization. If the shake of the camera is too strong (larger than the room for stabilization), some frames will not see the whole regions in the previous stabilized frames. If DIS discards this fact, then some parts of the output frame will remain not filled, as they are not visible in the input frame (technically such areas are extrapolated from the input image borders). This effect is referred to as “black corners” and DIS tries to avoid it, even if stabilization is compromised. 

.. doxygenstruct:: dis_config_t
   :project: media_library
   :members:
