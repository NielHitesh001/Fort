#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace luv {

struct HawkesParams {
    double base_intensity_mu{1.0};     // Baseline event arrival rate mu (events/sec)
    double alpha{2.0};                  // Self-excitation jump size alpha
    double beta{5.0};                   // Exponential decay rate beta
    double high_intensity_threshold{15.0}; // Toxic flow / cluster alert threshold
};

class HawkesProcessEstimator {
public:
    explicit HawkesProcessEstimator(const HawkesParams& params = {}) noexcept
        : params_(params), last_event_time_sec_(0.0), current_intensity_(params.base_intensity_mu) {}

    // Process a new arrival event at timestamp t (in seconds or normalized units)
    void on_event(double event_time_sec) noexcept {
        if (event_time_sec < last_event_time_sec_) return;

        double dt = event_time_sec - last_event_time_sec_;
        
        // Decay intensity over dt: lambda(t^-) = mu + (lambda(last) - mu) * exp(-beta * dt)
        double decayed_excess = (current_intensity_ - params_.base_intensity_mu) * std::exp(-params_.beta * dt);
        current_intensity_ = params_.base_intensity_mu + decayed_excess + params_.alpha;
        last_event_time_sec_ = event_time_sec;
    }

    // Evaluate intensity at current query time without recording a new event
    double get_intensity_at(double query_time_sec) const noexcept {
        if (query_time_sec <= last_event_time_sec_) {
            return current_intensity_;
        }
        double dt = query_time_sec - last_event_time_sec_;
        double decayed_excess = (current_intensity_ - params_.base_intensity_mu) * std::exp(-params_.beta * dt);
        return params_.base_intensity_mu + decayed_excess;
    }

    // Returns true if clustering intensity exceeds toxicity threshold
    bool is_toxic_cluster_active(double query_time_sec) const noexcept {
        return get_intensity_at(query_time_sec) >= params_.high_intensity_threshold;
    }

    // Branching ratio eta = alpha / beta (must be < 1.0 for stationary process)
    double get_branching_ratio() const noexcept {
        return (params_.beta > 0.0) ? (params_.alpha / params_.beta) : 0.0;
    }

    double get_current_intensity() const noexcept { return current_intensity_; }

private:
    HawkesParams params_;
    double last_event_time_sec_{0.0};
    double current_intensity_{1.0};
};

} // namespace luv
