/*
Copyright (C) 2014-2015 Thiemar Pty Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once


#include <cstring>
#include "fixed.h"
#include "park.h"
#include "esc_assert.h"


class StateEstimator {
    struct motor_state_t state_estimate_;

    /* Intermediate values */
    float a_; /* 1.0 - R / L * T */
    float b_; /* phi / L * T */
    float c_; /* T / L */
    float t_;
    float t_inv_;

    /* Current and speed lowpass filter parameters */
    float i_dq_lpf_coeff_;
    float angular_velocity_lpf_coeff_;

    /* Column-major */
    float state_covariance_[4];

    /* Intermediate state */
    float last_i_ab_a_[2];
    float next_sin_theta_;
    float next_cos_theta_;

public:
    StateEstimator():
        a_(0.0f),
        b_(0.0f),
        c_(0.0f),
        t_(0.0f),
        t_inv_(0.0f),
        i_dq_lpf_coeff_(0.0f),
        angular_velocity_lpf_coeff_(0.0f),
        next_sin_theta_(0.0f),
        next_cos_theta_(1.0f)
    {
        state_estimate_.angular_acceleration_rad_per_s2 = 0.0f;
        state_estimate_.angular_velocity_rad_per_s = 0.0f;
        state_estimate_.angle_rad = 0.0f;
        state_estimate_.i_dq_a[0] = state_estimate_.i_dq_a[1] = 0.0f;
        state_covariance_[0] = state_covariance_[1] = state_covariance_[2] =
            state_covariance_[3] = 0.0f;
        last_i_ab_a_[0] = last_i_ab_a_[1] = 0.0f;
    }

    void reset_state() {
        state_estimate_.angular_acceleration_rad_per_s2 = 0.0f;
        state_estimate_.angular_velocity_rad_per_s = 0.0f;
        state_estimate_.angle_rad = 0.0f;
        next_sin_theta_ = 0;
        next_cos_theta_ = 1.0f;
        last_i_ab_a_[0] = last_i_ab_a_[1] = 0.0f;
        state_covariance_[0] = state_covariance_[2] = 100.0f;
        state_covariance_[1] = state_covariance_[3] = 10.0f;
    }

    void update_state_estimate(
        const float i_ab_a[2],
        const float v_ab_v[2],
        float speed_setpoint,
        float closed_loop_frac
    );

    void get_state_estimate(struct motor_state_t& out_estimate) const {
        out_estimate = state_estimate_;
    }

    void __attribute__((optimize("O3")))
    get_est_v_alpha_beta_from_v_dq(
        float out_v_alpha_beta[2],
        const float in_v_dq[2]
    ) const {
        inverse_park_transform(out_v_alpha_beta, in_v_dq, next_sin_theta_,
                               next_cos_theta_);
    }

    void set_params(
        const struct motor_params_t& params,
        const struct control_params_t& control_params,
        float t_s
    ) {
        float wb;

        a_ = 1.0f - params.rs_r / params.ls_h * t_s;
        b_ = params.phi_v_s_per_rad / params.ls_h * t_s;
        c_ = t_s / params.ls_h;
        t_ = t_s;
        t_inv_ = 1.0f / t_s;

        /*
        Control parameters -- LPF corner frequency is one decade higher than
        the controller bandwidth; current control bandwidth is one decade
        higher than speed control bandwidth.
        */
        wb = 2.0f * (float)M_PI * control_params.bandwidth_hz;
        i_dq_lpf_coeff_ = 1.0f - fast_expf(-wb * t_s * 50.0f);
        angular_velocity_lpf_coeff_ = 1.0f - fast_expf(-wb * t_s);
    }
};


class ParameterEstimator {
    float sample_voltages_[4];
    float sample_currents_[4];

    float open_loop_angular_velocity_rad_per_s_;
    float open_loop_angle_rad_;

    float v_;
    float t_;
    uint16_t test_idx_;
    uint16_t open_loop_test_samples_;

public:
    ParameterEstimator():
        open_loop_angular_velocity_rad_per_s_(0.0f),
        open_loop_angle_rad_(0.0f),
        v_(0.0f),
        t_(0.0f),
        test_idx_(0),
        open_loop_test_samples_(0)
    {
        sample_voltages_[0] = sample_voltages_[1] = sample_voltages_[2] =
            sample_voltages_[3] = 0.0f;
        sample_currents_[0] = sample_currents_[1] = sample_currents_[2] =
            sample_currents_[3] = 0.0f;
    }

    void start_estimation(float t);

    void update_parameter_estimate(
        const float i_ab_a[2],
        const float v_ab_v[2]
    );

    void get_v_alpha_beta_v(float v_ab_v[2]);

    bool is_estimation_complete(void) const {
        return test_idx_ == 4;
    }

    void get_samples(float out_v[4], float out_i[4]) {
        memcpy(out_v, sample_voltages_, sizeof(sample_voltages_));
        memcpy(out_i, sample_currents_, sizeof(sample_currents_));
    }

    static void calculate_r_l_from_samples(
        float& r_r,
        float& l_h,
        const float v_sq[4],
        const float i_sq[4]
    );
};
