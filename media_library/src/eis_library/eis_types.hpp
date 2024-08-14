#pragma once

typedef struct gyro_sample
{
    int16_t vx;
    int16_t vy;
    int16_t vz;
    uint64_t timestamp_ns;
} gyro_sample_t;

typedef struct unbiased_gyro_sample {
    double vx;
    double vy;
    double vz;
    uint64_t timestamp_ns;

    unbiased_gyro_sample(double x, double y, double z, uint64_t timestamp)
        : vx(x), vy(y), vz(z), timestamp_ns(timestamp) {}

} unbiased_gyro_sample_t;