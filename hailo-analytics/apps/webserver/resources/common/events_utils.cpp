#include "events_utils.hpp"

namespace webserver
{
namespace resources
{

ProfileDigitalZoomState::ProfileDigitalZoomState(bool enable, uint32_t magnification)
    : ValuesState<bool, uint32_t>(enable, magnification)
{
}

bool ProfileDigitalZoomState::getEnable() const
{
    return std::get<0>(values);
}

uint32_t ProfileDigitalZoomState::getMagnification() const
{
    return std::get<1>(values);
}

ProfileDigitalZoomRoiState::ProfileDigitalZoomRoiState(bool enable, uint32_t magnification, double x, double y,
                                                       double width, double height)
    : ValuesState<bool, uint32_t, double, double, double, double>(enable, magnification, x, y, width, height)
{
}

bool ProfileDigitalZoomRoiState::getEnable() const
{
    return std::get<0>(values);
}

uint32_t ProfileDigitalZoomRoiState::getMagnification() const
{
    return std::get<1>(values);
}

double ProfileDigitalZoomRoiState::getX() const
{
    return std::get<2>(values);
}

double ProfileDigitalZoomRoiState::getY() const
{
    return std::get<3>(values);
}

double ProfileDigitalZoomRoiState::getWidth() const
{
    return std::get<4>(values);
}

double ProfileDigitalZoomRoiState::getHeight() const
{
    return std::get<5>(values);
}

} // namespace resources
} // namespace webserver
