#pragma once

#include <variant>
#include <string>

#include "service/storage_cleanup_service_ext.hpp"

// Strategy for different cleanup approaches
using CleanupParam = std::variant<float, int, std::string>;

class ICleanupStrategy
{
  public:
    virtual ~ICleanupStrategy() = default;
    virtual bool clean_up(StorageCleanupServiceExt &cleanup_service) = 0;
};

class FaissShardFirstCleanupStrategy : public ICleanupStrategy
{

  public:
    FaissShardFirstCleanupStrategy(float percent);

    bool clean_up(StorageCleanupServiceExt &cleanup_service) override;

  private:
    CleanupParam m_cleanup_param;
};
