#pragma once

#include "hailo_analytics/analytics/face_landmarks.hpp"

#define BBOX_CROP_STAGE "bbox_crops"
#define LANDMARKS_AI_STAGE "face_landmarks"
#define LANDMARKS_POST_STAGE "landmarks_post"
#define LANDMARKS_AGGREGATOR "landmarks_aggregator"

namespace hailo_analytics
{
PipelinePtr generate_face_landmarks_pipeline()
{
    /*
    // create pipeline builder
    PipelineBuilder pip_builder;

    // create stages
    std::shared_ptr<BBoxCropStage> bbox_crop_stage =
        BBoxCropStageBuild::create()... std::shared_ptr<HailortAsyncStage> landmarks_stage =
            HailortAsyncStageBuild::create()... std::shared_ptr<PostprocessStage> landmarks_post_stage =
                PostprocessStageBuild::create()... std::shared_ptr<AggregatorStage> landmarks_agg_stage =
                    AggregatorStageBuild::create()...

                    // add stages to pipeline builder
                    pip_builder.add_stage(bbox_crop_stage)
                        .add_stage(landmarks_stage)
                        .add_stage(landmarks_post_stage)
                        .add_stage(landmarks_agg_stage);

    // connect the stages withing the pipeline
    pip_builder.connect(BBOX_CROP_STAGE, LANDMARKS_AGGREGATOR)
        .connect(BBOX_CROP_STAGE, LANDMARKS_AI_STAGE)
        .connect(LANDMARKS_AI_STAGE, LANDMARKS_POST_STAGE)
        .connect(LANDMARKS_POST_STAGE, LANDMARKS_AGGREGATOR);

    // create the pipeline
    PipelinePtr pipeline = pip_builder.build();

    // set the input and output stages
    pipeline->set_in_stage(bbox_crop_stage);
    pipeline->set_out_stage(landmarks_agg_stage);

    return pipeline;
    */
    return nullptr; // TODO implement
}
} // namespace hailo_analytics
