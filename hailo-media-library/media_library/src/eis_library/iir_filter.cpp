#include <cmath>
#include "iir_filter.hpp"

IIRFilter::IIRFilter(double iir_coefficient, double gyro_scale, double bias)
    : prev_sample(0), prev_smooth(0), iir_coefficient(iir_coefficient), gyro_scale(gyro_scale), bias(bias),
      convergence_count(IIR_CONVERGENCE_COUNT), initialized(false)
{
    one_plus_beta_over_two = (1 + iir_coefficient) / 2;
}

void IIRFilter::reset()
{
    prev_sample = 0;
    prev_smooth = 0;
    initialized = false;
    convergence_count = IIR_CONVERGENCE_COUNT;
}

double IIRFilter::filter(double sample)
{
    double corrected_sample = sample * gyro_scale - bias;
    if (!initialized)
    {
        prev_smooth = corrected_sample;
        prev_sample = corrected_sample;
        initialized = true;
        return corrected_sample;
    }

    double output = iir_coefficient * prev_smooth + one_plus_beta_over_two * (corrected_sample - prev_sample);
    prev_sample = corrected_sample;
    prev_smooth = output;

    return output;
}

std::vector<double> IIRFilter::filter(std::vector<double> samples)
{
    std::vector<double> filtered_samples;
    filtered_samples.reserve(samples.size());

    for (const auto &sample : samples)
    {
        filtered_samples.push_back(filter(sample));
    }

    on_frame_end();
    return filtered_samples;
}

bool IIRFilter::converged()
{
    return convergence_count == 0;
}

bool IIRFilter::on_frame_end()
{
    if (convergence_count > 0)
    {
        convergence_count--;
    }
    return converged();
}
