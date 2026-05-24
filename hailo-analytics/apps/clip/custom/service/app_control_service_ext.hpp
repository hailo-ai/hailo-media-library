#pragma once

#include <tl/expected.hpp>
#include <stddef.h>

#include "hailo_analytics/analytics/reference_camera_app_constructor.hpp"

class AppControlServiceExt : public hailo_analytics::analytics::app_constructor::CameraAppExtension
{
  public:
    // Error types
    enum Error
    {
        INVALID_ACCESS,
        INVALID_PARAMETER,
    };

    // Constructor
    AppControlServiceExt() = default;

    // Destructor
    ~AppControlServiceExt() = default;

    tl::expected<bool, Error> set_clip_embedding_refresh_rate(size_t refresh_rate);

    tl::expected<size_t, Error> get_clip_embedding_refresh_rate();
};
