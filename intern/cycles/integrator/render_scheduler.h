/*
 * Copyright 2011-2021 Blender Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "integrator/adaptive_sampling.h"
#include "integrator/denoiser.h" /* For DenoiseParams. */
#include "render/buffers.h"

CCL_NAMESPACE_BEGIN

class RenderWork {
 public:
  int resolution_divider = 1;

  /* Path tracing samples information. */
  struct {
    int start_sample = 0;
    int num_samples = 0;

    bool adaptive_sampling_filter = false;
    float adaptive_sampling_threshold = 0.0f;
  } path_trace;

  bool denoise = false;

  /* Display which is used to visualize render result is to be updated for the new render. */
  bool update_display = false;

  /* Conversion to bool, to simplify checks about whether there is anything to be done for this
   * work. */
  inline operator bool() const
  {
    return path_trace.num_samples || path_trace.adaptive_sampling_filter || denoise ||
           update_display;
  }
};

class RenderScheduler {
 public:
  RenderScheduler(bool headless, bool background, int pixel_size);

  bool is_background() const;

  void set_denoiser_params(const DenoiseParams &params);
  void set_adaptive_sampling(const AdaptiveSampling &adaptive_sampling);

  /* Start sample for path tracing.
   * The scheduler will schedule work using this sample as the first one. */
  void set_start_sample(int start_sample);
  int get_start_sample() const;

  /* Number of sampels to render, starting from start sample.
   * The scheduler will schedule work in the range of
   * [start_sample, start_sample + num_samples - 1], inclusively. */
  void set_num_samples(int num_samples);
  int get_num_samples() const;

  /* Get sample up to which rendering has been done.
   * This is an absolute 0-based value.
   *
   * For example, if start sample is 10 and and 5 samples were rendered, then this call will
   * return 14.
   *
   * If there were no samples rendered, then the behavior is undefined. */
  int get_rendered_sample() const;

  /* Get number of samples rendered within the current scheduling session.
   *
   * For example, if start sample is 10 and and 5 samples were rendered, then this call will
   * return 5.
   *
   * Note that this is based on the scheduling information. In practice this means that if someone
   * requested for work to render the scheduler considers the work done. */
  int get_num_rendered_samples() const;

  /* Reset scheduler, indicating that rendering will happen from scratch.
   * Resets current rendered state, as well as scheduling information. */
  void reset(const BufferParams &buffer_params, int num_samples);

  /* Indicate that path tracing has finished (due to adaptive sampling convergency). Only remaining
   * tasks like denoising will be scheduled after this. */
  void set_path_trace_finished(RenderWork &render_work);

  /* Check whether all work has been scheduled. */
  bool done() const;

  RenderWork get_render_work();

  /* Report time (in seconds) which corresponding part of work took. */
  void report_path_trace_time(const RenderWork &render_work, double time);
  void report_denoise_time(const RenderWork &render_work, double time);
  void report_display_update_time(const RenderWork &render_work, double time);

 protected:
  /* Update start resolution divider based on the accumulated timing information, preserving nice
   * feeling navigation feel. */
  void update_start_resolution_divider();

  /* Calculate desired update interval in seconds based on the current timings and settings.
   * Will give an interval which provides good feeling updates during viewport navigation. */
  double guess_viewport_navigation_update_interval_in_seconds() const;

  /* Check whether denoising is active during interactive update while resolution divider is not
   * unit. */
  bool is_denoise_active_during_update() const;

  /* Heuristic which aims to give perceptually pleasant update of display interval in a way that at
   * lower samples updates happens more often, but with higher number of samples updates happens
   * less often but the device occupancy goes higher. */
  double guess_display_update_interval_in_seconds() const;
  double guess_display_update_interval_in_seconds_for_num_samples(int num_rendered_samples) const;

  /* Calculate number of samples which can be rendered within current desred update interval which
   * is calculated by `guess_update_interval_in_seconds()`. */
  int calculate_num_samples_per_update() const;

  /* Get start sample and the number of samples which are to be path traces in the current work. */
  int get_start_sample_to_path_trace() const;
  int get_num_samples_to_path_trace() const;

  /* Calculate how many samples there are to be rendered for the very first path trace after reset.
   */
  int get_num_samples_during_navigation(int resolution_divier) const;

  /* Check whether current work needs denoising.
   * Denoising is not needed if the denoiser is not configured, or when denosiing is happening too
   * often.
   *
   * The delayed will be true when the denoiser is configured for use, but it was delayed for a
   * later sample, to reduce overhead. */
  bool work_need_denoise(bool &delayed);

  /* Check whether current work need to update display.
   *
   * The `denoiser_delayed` is what `work_need_denoise()` returned as delayed denoiser flag. */
  bool work_need_update_display(const bool denoiser_delayed);

  /* Check whether timing of the given work are usable to store timings in the `first_render_time_`
   * for the resolution divider calculation. */
  bool work_is_usable_for_first_render_estimation(const RenderWork &render_work);

  struct TimeAverage {
    inline void reset()
    {
      total_time = 0.0;
      num_measured_times = 0;
    }

    inline double get_average() const
    {
      return total_time / num_measured_times;
    }

    double total_time = 0.0;
    int num_measured_times = 0;
  };

  struct {
    int resolution_divider = 1;

    /* Number of rendered samples on top of the start sample. */
    int num_rendered_samples = 0;

    /* Point in time the latest GPUDisplay work has been scheduled. */
    double last_display_update_time = 0.0;
    /* Value of -1 means display was never updated. */
    int last_display_update_sample = -1;

    bool path_trace_finished = false;
  } state_;

  /* Timing of tasks which were performed at the very first render work at 100% of the
   * resolution. This timing information is used to estimate resolution divider for fats
   * navigation. */
  struct {
    double path_trace_per_sample;
    double denoise_time;
    double display_update_time;
  } first_render_time_;

  TimeAverage path_trace_time_;
  TimeAverage denoise_time_;
  TimeAverage display_update_time_;

  /* Path tracing work will be scheduled for samples from within
   * [start_sample_, start_sample_ + num_samples_ - 1] range, inclusively. */
  int start_sample_ = 0;
  int num_samples_ = 0;

  /* Headless rendering without interface. */
  bool headless_;

  /* Background (offline) rendering. */
  bool background_;

  /* Pixel size is used to force lower resolution render for final pass. Useful for retina or other
   * types of hi-dpi displays. */
  int pixel_size_ = 1;

  BufferParams buffer_params_;
  DenoiseParams denoiser_params_;
  AdaptiveSampling adaptive_sampling_;

  /* Default value for the resolution divider which will be used when there is no render time
   * information available yet.
   * It is also what defines the upper limit of the automatically calculated resolution divider. */
  int default_start_resolution_divider_ = 1;

  /* Initial resolution divider which will be used on render scheduler reset. */
  int start_resolution_divider_ = 0;
};

/* Calculate smallest resolution divider which will bring down actual rendering time below the
 * desired one. This call assumes linear dependency of render time from number of pixel (quadratic
 * dependency from the resolution divider): resolution divider of 2 beings render time down by a
 * factor of 4. */
int calculate_resolution_divider_for_time(double desired_time, double actual_time);

int calculate_resolution_divider_for_resolution(int width, int height, int resolution);

int calculate_resolution_for_divider(int width, int height, int resolution_divider);

CCL_NAMESPACE_END
