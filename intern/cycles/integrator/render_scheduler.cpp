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

#include "integrator/render_scheduler.h"

#include "util/util_logging.h"
#include "util/util_math.h"
#include "util/util_time.h"

CCL_NAMESPACE_BEGIN

/* --------------------------------------------------------------------
 * Render scheduler.
 */

RenderScheduler::RenderScheduler(bool headless, bool background, int pixel_size)
    : headless_(headless), background_(background), pixel_size_(pixel_size)
{
}

bool RenderScheduler::is_background() const
{
  return background_;
}

void RenderScheduler::set_denoiser_params(const DenoiseParams &params)
{
  denoiser_params_ = params;
}

void RenderScheduler::set_adaptive_sampling(const AdaptiveSampling &adaptive_sampling)
{
  adaptive_sampling_ = adaptive_sampling;
}

void RenderScheduler::set_start_sample(int start_sample)
{
  start_sample_ = start_sample;
}

int RenderScheduler::get_start_sample() const
{
  return start_sample_;
}

void RenderScheduler::set_num_samples(int num_samples)
{
  num_samples_ = num_samples;
}

int RenderScheduler::get_num_samples() const
{
  return num_samples_;
}

int RenderScheduler::get_rendered_sample() const
{
  DCHECK_GT(get_num_rendered_samples(), 0);

  return start_sample_ + get_num_rendered_samples() - 1;
}

int RenderScheduler::get_num_rendered_samples() const
{
  return state_.num_rendered_samples;
}

void RenderScheduler::reset(const BufferParams &buffer_params, int num_samples)
{
  buffer_params_ = buffer_params;

  update_start_resolution_divider();

  set_num_samples(num_samples);

  /* In background mode never do lower resolution render preview, as it is not really supported
   * by the software. */
  if (background_) {
    state_.resolution_divider = 1;
  }
  else {
    /* NOTE: Divide by 2 because of the way how scheduling works: it advances resolution divider
     * first and then initialized render work. */
    state_.resolution_divider = start_resolution_divider_ * 2;
  }

  state_.num_rendered_samples = 0;
  state_.last_display_update_time = 0.0;
  state_.last_display_update_sample = -1;

  first_render_time_.path_trace = 0.0;
  first_render_time_.denoise_time = 0.0;
  first_render_time_.display_update_time = 0.0;

  path_trace_time_.reset();
  denoise_time_.reset();
  display_update_time_.reset();
}

bool RenderScheduler::done() const
{
  if (state_.resolution_divider != pixel_size_) {
    return false;
  }

  return get_num_rendered_samples() >= num_samples_;
}

RenderWork RenderScheduler::get_render_work()
{
  if (done()) {
    return RenderWork();
  }

  RenderWork render_work;

  if (state_.resolution_divider != pixel_size_) {
    state_.resolution_divider = max(state_.resolution_divider / 2, pixel_size_);
    state_.num_rendered_samples = 0;
    state_.last_display_update_sample = -1;
  }

  render_work.resolution_divider = state_.resolution_divider;

  render_work.path_trace.start_sample = get_start_sample_to_path_trace();
  render_work.path_trace.num_samples = get_num_samples_to_path_trace();

  /* NOTE: Advance number of samples now, so that filter and denoising check can see that all the
   * samples are rendered. */
  state_.num_rendered_samples += render_work.path_trace.num_samples;

  render_work.path_trace.adaptive_sampling_filter = adaptive_sampling_.need_filter(
      get_rendered_sample());

  bool denoiser_delayed;
  render_work.denoise = work_need_denoise(denoiser_delayed);

  render_work.update_display = work_need_update_display(denoiser_delayed);

  /* A fallback display update time, for the case there is an error of display update, or when
   * there is no display at all. */
  if (render_work.update_display) {
    state_.last_display_update_time = time_dt();
    state_.last_display_update_sample = state_.num_rendered_samples;
  }

  return render_work;
}

/* Knowing time which it took to complete a task at the current resolution divider approximate how
 * long it would have taken to complete it at a final resolution. */
static double approximate_final_time(const RenderWork &render_work, double time)
{
  if (render_work.resolution_divider == 1) {
    return time;
  }

  const double resolution_divider_sq = render_work.resolution_divider *
                                       render_work.resolution_divider;
  return time * resolution_divider_sq;
}

void RenderScheduler::report_path_trace_time(const RenderWork &render_work, double time)
{
  const double final_time_approx = approximate_final_time(render_work, time);

  if (work_is_usable_for_first_render_estimation(render_work)) {
    first_render_time_.path_trace = final_time_approx;
  }

  path_trace_time_.total_time += final_time_approx;
  path_trace_time_.num_measured_times += render_work.path_trace.num_samples;

  VLOG(4) << "Average path tracing time: " << path_trace_time_.get_average() << " seconds.";
}

void RenderScheduler::report_denoise_time(const RenderWork &render_work, double time)
{
  const double final_time_approx = approximate_final_time(render_work, time);

  if (work_is_usable_for_first_render_estimation(render_work)) {
    first_render_time_.denoise_time = final_time_approx;
  }

  denoise_time_.total_time += final_time_approx;
  ++denoise_time_.num_measured_times;

  VLOG(4) << "Average denoising time: " << denoise_time_.get_average() << " seconds.";
}

void RenderScheduler::report_display_update_time(const RenderWork &render_work, double time)
{
  const double final_time_approx = approximate_final_time(render_work, time);

  if (work_is_usable_for_first_render_estimation(render_work)) {
    first_render_time_.display_update_time = final_time_approx;
  }

  display_update_time_.total_time += final_time_approx;
  ++display_update_time_.num_measured_times;

  VLOG(4) << "Average display update time: " << display_update_time_.get_average() << " seconds.";

  /* Move the display update moment further in time, so that logic which checks when last update
   * did happen have more reliable point in time (without path tracing and denoising parts of the
   * render work). */
  state_.last_display_update_time = time_dt();
}

double RenderScheduler::guess_display_update_interval_in_seconds() const
{
  return guess_display_update_interval_in_seconds_for_num_samples(state_.num_rendered_samples);
}

/* TODO(sergey): This is just a quick implementation, exact values might need to be tweaked based
 * on a more careful experiments with viewport rendering. */
double RenderScheduler::guess_display_update_interval_in_seconds_for_num_samples(
    int num_rendered_samples) const
{
  /* TODO(sergey): Need a decision on whether this should be using number of samples rendered
   * within the current render ression, or use absolute number of samples with the start sample
   * taken into account. It will depend on whether the start sample offset clears the render
   * buffer. */

  if (headless_) {
    /* In headless mode do rare updates, so that the device occupancy is high, but there are still
     * progress messages printed to the logs. */
    return 30.0;
  }

  if (background_) {
    if (num_rendered_samples < 32) {
      return 1.0;
    }
    return 2.0;
  }

  if (num_rendered_samples < 4) {
    return 0.1;
  }
  if (num_rendered_samples < 8) {
    return 0.25;
  }
  if (num_rendered_samples < 16) {
    return 0.5;
  }
  if (num_rendered_samples < 32) {
    return 1.0;
  }
  return 2.0;
}

int RenderScheduler::calculate_num_samples_per_update() const
{
  const double time_per_sample_average = path_trace_time_.get_average();
  const double num_samples_in_second = 1.0 / time_per_sample_average;

  const double update_interval_in_seconds = guess_display_update_interval_in_seconds();

  return max(int(num_samples_in_second * update_interval_in_seconds), 1);
}

int RenderScheduler::get_start_sample_to_path_trace() const
{
  return start_sample_ + state_.num_rendered_samples;
}

/* Round number of samples to the closest power of two.
 * Rounding might happen to higher or lower value depending on which one is closer. Such behavior
 * allows to have number of samples to be power of two without diverging from the planned number of
 * samples too much. */
static inline uint round_num_samples_to_power_of_2(const uint num_samples)
{
  if (num_samples == 1) {
    return 1;
  }

  if (is_power_of_two(num_samples)) {
    return num_samples;
  }

  const uint num_samples_up = next_power_of_two(num_samples);
  const uint num_samples_down = num_samples_up - (num_samples_up >> 1);

  const uint delta_up = num_samples_up - num_samples;
  const uint delta_down = num_samples - num_samples_down;

  if (delta_up <= delta_down) {
    return num_samples_up;
  }

  return num_samples_down;
}

int RenderScheduler::get_num_samples_to_path_trace() const
{
  /* Specvial trick for the fast navigation: schedule multiple samples during fats navigation
   * (which will prefer to use lower resolution to keep up with refresh rate). This gives more
   * usable visual feedback for artists. There are couple of tricks though:
   *
   * - When resolution divider is the previous to the final resolution schedule single sample.
   *   This is so that rendering on lower resolution does not exceed time what it takes to render
   *   first sample at the full resolution.
   *
   * - When denoising is used during navigation prefer using highetr resolution and less samples
   *  (scheduling less samples here will make it so resolutiondivider calculation will use lower
   *  value for the divider). This is because both OpenImageDenoiser and OptiX denoiser gives
   * visually better results on higher resolution image with less samples. */
  if (state_.resolution_divider != pixel_size_) {
    if (state_.resolution_divider != pixel_size_ * 2 && !is_denoise_active_during_update()) {
      return min(num_samples_, kNumSamplesDuringUpdate);
    }

    return 1;
  }

  /* Always start full resolution render  with a single sample. Gives more instant feedback to
   * artists, and allows to gather information for a subsequent path tracing works. Do it in the
   * headless mode as well, to give some estimate of how long samples are taking. */
  if (state_.num_rendered_samples == 0) {
    return 1;
  }

  const int num_samples_per_update = calculate_num_samples_per_update();
  const int path_trace_start_sample = get_start_sample_to_path_trace();

  /* Round number of samples to a power of two, so that division of path states into tiles goes in
   * a more integer manner.
   * This might make it so updates happens more rarely due to rounding up. In the test scenes this
   * is not huge deal because it is not seen that more than 8 sampels can be rendered between
   * updates. If that becomes a problem we can add some extra rules like never allow to round up
   * more than N samples. */
  const int num_samples_pot = round_num_samples_to_power_of_2(num_samples_per_update);

  const int num_samples_to_render = min(num_samples_pot,
                                        start_sample_ + num_samples_ - path_trace_start_sample);

  /* If adaptive sampling is not use, render as many samples per update as possible, keeping the
   * device fully occupied, without much overhead of display updates. */
  if (!adaptive_sampling_.use) {
    return num_samples_to_render;
  }

  /* TODO(sergey): Add extra "clamping" here so that none of the filtering points is missing. This
   * is to ensure that the final render is pixel-matched regardless of how many samples per second
   * compute device can do. */

  return adaptive_sampling_.align_samples(path_trace_start_sample, num_samples_to_render);
}

bool RenderScheduler::work_need_denoise(bool &delayed)
{
  delayed = false;

  if (!denoiser_params_.use) {
    /* Denoising is disabled, no need to scheduler work for it. */
    return false;
  }

  if (background_) {
    /* Background render, only denoise when rendering the last sample. */
    /* TODO(sergey): Follow similar logic to viewport, giving an overview of how final denoised
     * image looks like even for the background rendering. */
    return done();
  }

  /* Viewport render. */

  /* Immediately denoise when we reach the start sample or last sample. */
  const int num_samples_finished = state_.num_rendered_samples;
  if (num_samples_finished == denoiser_params_.start_sample ||
      num_samples_finished == num_samples_) {
    return true;
  }

  /* Do not denoise until the sample at which denoising should start is reached. */
  if (num_samples_finished < denoiser_params_.start_sample) {
    return false;
  }

  /* Avoid excessive denoising in viewport after reaching a certain amount of samples. */
  /* TODO(sergey): Consider making time interval and sample configurable. */
  delayed = (state_.num_rendered_samples >= 20 &&
             (time_dt() - state_.last_display_update_time) < 1.0);

  return !delayed;
}

bool RenderScheduler::work_need_update_display(const bool denoiser_delayed)
{
  if (headless_) {
    /* Force disable display update in headless mode. There will be nothing to display the
     * in-progress result. */
    return false;
  }

  if (denoiser_delayed) {
    /* If denoiser has been delayed the display can not be updated as it will not contain
     * up-to-date state of the render result. */
    return false;
  }

  if (!adaptive_sampling_.use) {
    /* When adaptive sampling is not used the work is scheduled in a way that they keep render
     * device busy for long enough, so that the display update can happen right after the
     * rendering. */
    return true;
  }

  if (done() || state_.last_display_update_sample == -1) {
    /* Make sure an initial and final results of adaptive sampling is communicated ot the display.
     */
    return true;
  }

  /* When adaptive sampling is used, its possible that only handful of samples of a very simple
   * scene will be scheduled to a powerful device (in order to not "miss" any of filtering points).
   * We take care of skipping updates here based on when previous display update did happen. */
  const double update_interval = guess_display_update_interval_in_seconds_for_num_samples(
      state_.last_display_update_sample);
  return (time_dt() - state_.last_display_update_time) > update_interval;
}

void RenderScheduler::update_start_resolution_divider()
{
  const int default_resolution_divider = calculate_resolution_divider_for_resolution(
      buffer_params_.width, buffer_params_.height, kDefaultStartResolution);

  if (start_resolution_divider_ == 0) {
    /* Resolution divider has never been calculated before: use default resolution, so that we have
     * somewhat good initial behavior, giving a chance to collect real numbers. */
    start_resolution_divider_ = default_resolution_divider;
    VLOG(3) << "Initial resolution divider is " << start_resolution_divider_;
    return;
  }

  if (first_render_time_.path_trace == 0.0) {
    /* Not enough information to calculate better resolution, keep the existing one. */
    return;
  }

  const double desired_update_interval_in_seconds =
      guess_viewport_navigation_update_interval_in_seconds();

  const double actual_time_per_update = first_render_time_.path_trace +
                                        first_render_time_.denoise_time +
                                        first_render_time_.display_update_time;

  /* Allow some percent of tolerance, so that if the render time is close enough to the higher
   * resolution we prefer to use it instead of going way lower resolution and time way below the
   * desired one. */
  const int resolution_divider_for_update = calculate_resolution_divider_for_time(
      desired_update_interval_in_seconds * 1.4, actual_time_per_update);

  /* Never higher resolution that the pixel size allows to (which is possible if the scene is
   * simple and compute device is fast). */
  const int new_resolution_divider = max(resolution_divider_for_update, pixel_size_);

  /* TODO(sergey): Need to add hysteresis to avoid resolution divider bouncing around when actual
   * render time is somewhere on a boundary between two resolutions. */

  /* Don't let resolution to go below the desired one: better be slower than provide a fully
   * unreadable viewport render. */
  start_resolution_divider_ = min(new_resolution_divider, default_resolution_divider);

  VLOG(3) << "Calculated resolution divider is " << start_resolution_divider_;
}

double RenderScheduler::guess_viewport_navigation_update_interval_in_seconds() const
{
  if (is_denoise_active_during_update()) {
    /* Use lower value than the non-denoised case to allow having more pixels to reconstruct the
     * image from. With the faster updates and extra compute required the resolution becomes too
     * low to give usable feedback. */
    /* NOTE: Based on performance of OpenImageDenoiser on CPU. For OptiX denoiser or other denoiser
     * on GPU the value might need to become lower for faster navigation. */
    return 1.0 / 12.0;
  }

  /* For the best match with the Blender's viewport the refresh ratio should be 60fps. This will
   * avoid "jelly" effects. However, on a non-trivial scenes this can only be achieved with high
   * values of the resolution divider which does not give very pleasant updates during navigation.
   * Choose less frequent updates to allow more noise-free and higher resolution updates. */

  /* TODO(sergey): Can look into heuristic which will allow to have 60fps if the resolution divider
   * is not too high. Alternatively, synchronize Blender's overlays updates to Cycles updates. */

  return 1.0 / 30.0;
}

bool RenderScheduler::is_denoise_active_during_update() const
{
  if (!denoiser_params_.use) {
    return false;
  }

  if (denoiser_params_.start_sample > 1) {
    return false;
  }

  return true;
}

bool RenderScheduler::work_is_usable_for_first_render_estimation(const RenderWork &render_work)
{
  return render_work.resolution_divider == pixel_size_ &&
         render_work.path_trace.start_sample == start_sample_;
}

/* --------------------------------------------------------------------
 * Utility functions.
 */

int calculate_resolution_divider_for_time(double desired_time, double actual_time)
{
  /* TODO(sergey): There should a non-iterative analytical formula here. */

  int resolution_divider = 1;
  while (actual_time > desired_time) {
    resolution_divider = resolution_divider * 2;
    actual_time /= 4.0;
  }

  return resolution_divider;
}

int calculate_resolution_divider_for_resolution(int width, int height, int resolution)
{
  if (resolution == INT_MAX) {
    return 1;
  }

  int resolution_divider = 1;
  while (width * height > resolution * resolution) {
    width = max(1, width / 2);
    height = max(1, height / 2);

    resolution_divider <<= 1;
  }

  return resolution_divider;
}

int calculate_resolution_for_divider(int width, int height, int resolution_divider)
{
  const int pixel_area = width * height;
  const int resolution = lround(sqrt(pixel_area));

  return resolution / resolution_divider;
}

CCL_NAMESPACE_END
