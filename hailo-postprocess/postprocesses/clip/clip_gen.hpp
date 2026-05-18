/**
 * Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/
#pragma once
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"

__BEGIN_DECLS
void filter(HailoROIPtr roi);
void clip_resnet_50x4(HailoROIPtr roi);
void clip_vit_b_32(HailoROIPtr roi);
void clip_vit_l_14_laion2B(HailoROIPtr roi);
__END_DECLS
