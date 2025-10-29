#pragma once
#include <cstddef>
#include <vector>

#define IIR_CONVERGENCE_COUNT 60

class IIRFilter
{
  public:
    IIRFilter(double iir_coefficient, double gyro_scale, double bias);
    void initialize();
    void reset();
    bool converged();
    bool on_frame_end();
    std::vector<double> filter(std::vector<double> samples);
    double filter(double sample);

  private:
    double prev_sample;
    double prev_smooth;
    double iir_coefficient;
    double gyro_scale;
    double bias;
    double one_plus_beta_over_two;

    size_t convergence_count;
    bool initialized;
};
