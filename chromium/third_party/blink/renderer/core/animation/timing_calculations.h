/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_ANIMATION_TIMING_CALCULATIONS_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_ANIMATION_TIMING_CALCULATIONS_H_

#include "third_party/blink/renderer/core/animation/animation_effect.h"
#include "third_party/blink/renderer/core/animation/timing.h"
#include "third_party/blink/renderer/platform/wtf/math_extras.h"

namespace blink {

namespace {
inline bool IsWithinEpsilon(double a, double b) {
  // Permit 2-bits of quantization error. Threshold based on experimentation
  // with accuracy of fmod.
  return std::abs(a - b) <= 2.0 * std::numeric_limits<double>::epsilon();
}

inline bool LessThanOrEqualToWithinEpsilon(double a, double b) {
  return a <= b || IsWithinEpsilon(a, b);
}

inline bool EndsOnIterationBoundary(double iteration_count,
                                    double iteration_start) {
  DCHECK(std::isfinite(iteration_count));
  return !fmod(iteration_count + iteration_start, 1);
}
}  // namespace

static inline double MultiplyZeroAlwaysGivesZero(double x, double y) {
  DCHECK(!IsNull(x));
  DCHECK(!IsNull(y));
  return x && y ? x * y : 0;
}

static inline double MultiplyZeroAlwaysGivesZero(AnimationTimeDelta x,
                                                 double y) {
  DCHECK(!IsNull(y));
  return x.is_zero() || y == 0 ? 0 : (x * y).InSecondsF();
}

// https://drafts.csswg.org/web-animations-1/#animation-effect-phases-and-states
static inline Timing::Phase CalculatePhase(
    double active_duration,
    double local_time,
    AnimationEffect::AnimationDirection direction,
    const Timing& specified) {
  DCHECK_GE(active_duration, 0);
  if (IsNull(local_time))
    return Timing::kPhaseNone;
  double end_time = std::max(
      specified.start_delay + active_duration + specified.end_delay, 0.0);
  double before_active_boundary_time =
      std::max(std::min(specified.start_delay, end_time), 0.0);
  if (local_time < before_active_boundary_time ||
      (local_time == before_active_boundary_time &&
       direction == AnimationEffect::AnimationDirection::kBackwards)) {
    return Timing::kPhaseBefore;
  }
  double active_after_boundary_time = std::max(
      std::min(specified.start_delay + active_duration, end_time), 0.0);
  if (local_time > active_after_boundary_time ||
      (local_time == active_after_boundary_time &&
       direction == AnimationEffect::AnimationDirection::kForwards)) {
    return Timing::kPhaseAfter;
  }
  return Timing::kPhaseActive;
}

// https://drafts.csswg.org/web-animations/#calculating-the-active-time
static inline double CalculateActiveTime(double active_duration,
                                         Timing::FillMode fill_mode,
                                         double local_time,
                                         Timing::Phase phase,
                                         const Timing& specified) {
  DCHECK_GE(active_duration, 0);

  switch (phase) {
    case Timing::kPhaseBefore:
      if (fill_mode == Timing::FillMode::BACKWARDS ||
          fill_mode == Timing::FillMode::BOTH)
        return std::max(local_time - specified.start_delay, 0.0);
      return NullValue();
    case Timing::kPhaseActive:
      return local_time - specified.start_delay;
    case Timing::kPhaseAfter:
      if (fill_mode == Timing::FillMode::FORWARDS ||
          fill_mode == Timing::FillMode::BOTH) {
        return std::max(
            0.0, std::min(active_duration, local_time - specified.start_delay));
      }
      return NullValue();
    case Timing::kPhaseNone:
      DCHECK(IsNull(local_time));
      return NullValue();
    default:
      NOTREACHED();
      return NullValue();
  }
}

// Calculates the overall progress, which describes the number of iterations
// that have completed (including partial iterations).
// https://drafts.csswg.org/web-animations/#calculating-the-overall-progress
static inline double CalculateOverallProgress(Timing::Phase phase,
                                              double active_time,
                                              double iteration_duration,
                                              double iteration_count,
                                              double iteration_start) {
  // 1. If the active time is unresolved, return unresolved.
  if (IsNull(active_time))
    return NullValue();

  // 2. Calculate an initial value for overall progress.
  double overall_progress = 0;
  if (IsWithinEpsilon(iteration_duration, 0)) {
    if (phase != Timing::kPhaseBefore)
      overall_progress = iteration_count;
  } else {
    overall_progress = active_time / iteration_duration;
  }

  return overall_progress + iteration_start;
}

// Calculates the simple iteration progress, which is a fraction of the progress
// through the current iteration that ignores transformations to the time
// introduced by the playback direction or timing functions applied to the
// effect.
// https://drafts.csswg.org/web-animations/#calculating-the-simple-iteration
// -progress
static inline double CalculateSimpleIterationProgress(Timing::Phase phase,
                                                      double overall_progress,
                                                      double iteration_start,
                                                      double active_time,
                                                      double active_duration,
                                                      double iteration_count) {
  // 1. If the overall progress is unresolved, return unresolved.
  if (IsNull(overall_progress))
    return NullValue();

  // 2. If overall progress is infinity, let the simple iteration progress be
  // iteration start % 1.0, otherwise, let the simple iteration progress be
  // overall progress % 1.0.
  double simple_iteration_progress = std::isinf(overall_progress)
                                         ? fmod(iteration_start, 1.0)
                                         : fmod(overall_progress, 1.0);

  // 3. If all of the following conditions are true,
  //   * the simple iteration progress calculated above is zero, and
  //   * the animation effect is in the active phase or the after phase, and
  //   * the active time is equal to the active duration, and
  //   * the iteration count is not equal to zero.
  // let the simple iteration progress be 1.0.
  if (IsWithinEpsilon(simple_iteration_progress, 0.0) &&
      (phase == Timing::kPhaseActive || phase == Timing::kPhaseAfter) &&
      IsWithinEpsilon(active_time, active_duration) &&
      !IsWithinEpsilon(iteration_count, 0.0)) {
    simple_iteration_progress = 1.0;
  }

  // 4. Return simple iteration progress.
  return simple_iteration_progress;
}

// https://drafts.csswg.org/web-animations/#calculating-the-current-iteration
static inline double CalculateCurrentIteration(
    Timing::Phase phase,
    double active_time,
    double iteration_count,
    double overall_progress,
    double simple_iteration_progress) {
  // 1. If the active time is unresolved, return unresolved.
  if (IsNull(active_time))
    return NullValue();

  // 2. If the animation effect is in the after phase and the iteration count
  // is infinity, return infinity.
  if (phase == Timing::kPhaseAfter && std::isinf(iteration_count)) {
    return std::numeric_limits<double>::infinity();
  }

  // 3. If the simple iteration progress is 1.0, return floor(overall progress)
  // - 1.
  if (simple_iteration_progress == 1.0) {
    // Safeguard for zero duration animation (crbug.com/954558).
    return fmax(0, floor(overall_progress) - 1);
  }

  // 4. Otherwise, return floor(overall progress).
  return floor(overall_progress);
}

// https://drafts.csswg.org/web-animations/#calculating-the-directed-progress
static inline bool IsCurrentDirectionForwards(
    double current_iteration,
    Timing::PlaybackDirection direction) {
  const bool current_iteration_is_even =
      std::isinf(current_iteration)
          ? true
          : IsWithinEpsilon(fmod(current_iteration, 2), 0);

  switch (direction) {
    case Timing::PlaybackDirection::NORMAL:
      return true;

    case Timing::PlaybackDirection::REVERSE:
      return false;

    case Timing::PlaybackDirection::ALTERNATE_NORMAL:
      return current_iteration_is_even;

    case Timing::PlaybackDirection::ALTERNATE_REVERSE:
      return !current_iteration_is_even;
  }
}

// https://drafts.csswg.org/web-animations/#calculating-the-directed-progress
static inline double CalculateDirectedProgress(
    double simple_iteration_progress,
    double current_iteration,
    Timing::PlaybackDirection direction) {
  // 1. If the simple progress is unresolved, return unresolved.
  if (IsNull(simple_iteration_progress))
    return NullValue();

  // 2. Calculate the current direction.
  bool current_direction_is_forwards =
      IsCurrentDirectionForwards(current_iteration, direction);

  // 3. If the current direction is forwards then return the simple iteration
  // progress. Otherwise return 1 - simple iteration progress.
  return current_direction_is_forwards ? simple_iteration_progress
                                       : 1 - simple_iteration_progress;
}

// https://drafts.csswg.org/web-animations/#calculating-the-transformed-progress
static inline double CalculateTransformedProgress(
    Timing::Phase phase,
    double directed_progress,
    double iteration_duration,
    bool is_current_direction_forward,
    scoped_refptr<TimingFunction> timing_function) {
  if (IsNull(directed_progress))
    return NullValue();

  // Set the before flag to indicate if at the leading edge of an iteration.
  // This is used to determine if the left or right limit should be used if at a
  // discontinuity in the timing function.
  bool before = is_current_direction_forward ? phase == Timing::kPhaseBefore
                                             : phase == Timing::kPhaseAfter;
  TimingFunction::LimitDirection limit_direction =
      before ? TimingFunction::LimitDirection::LEFT
             : TimingFunction::LimitDirection::RIGHT;

  // Snap boundaries to correctly render step timing functions at 0 and 1.
  // (crbug.com/949373)
  if (phase == Timing::kPhaseAfter) {
    if (is_current_direction_forward && IsWithinEpsilon(directed_progress, 1)) {
      directed_progress = 1;
    } else if (!is_current_direction_forward &&
               IsWithinEpsilon(directed_progress, 0)) {
      directed_progress = 0;
    }
  }

  // Return the result of evaluating the animation effect’s timing function
  // passing directed progress as the input progress value.
  return timing_function->Evaluate(directed_progress, limit_direction);
}

// Offsets the active time by how far into the animation we start (i.e. the
// product of the iteration start and iteration duration). This is not part of
// the Web Animations spec; it is used for calculating the time until the next
// iteration to optimize scheduling.
static inline double CalculateOffsetActiveTime(double active_duration,
                                               double active_time,
                                               double start_offset) {
  DCHECK_GE(active_duration, 0);
  DCHECK_GE(start_offset, 0);

  if (IsNull(active_time))
    return NullValue();

  DCHECK(active_time >= 0 &&
         LessThanOrEqualToWithinEpsilon(active_time, active_duration));

  if (!std::isfinite(active_time))
    return std::numeric_limits<double>::infinity();

  return active_time + start_offset;
}

// Maps the offset active time into 'iteration time space'[0], aka the offset
// into the current iteration. This is not part of the Web Animations spec (note
// that the section linked below is non-normative); it is used for calculating
// the time until the next iteration to optimize scheduling.
//
// [0] https://drafts.csswg.org/web-animations-1/#iteration-time-space
static inline double CalculateIterationTime(double iteration_duration,
                                            double active_duration,
                                            double offset_active_time,
                                            double start_offset,
                                            Timing::Phase phase,
                                            const Timing& specified) {
  DCHECK_GT(iteration_duration, 0);
  DCHECK_EQ(active_duration,
            MultiplyZeroAlwaysGivesZero(iteration_duration,
                                        specified.iteration_count));

  if (IsNull(offset_active_time))
    return NullValue();

  DCHECK_GE(offset_active_time, 0);
  DCHECK(LessThanOrEqualToWithinEpsilon(offset_active_time,
                                        active_duration + start_offset));

  if (!std::isfinite(offset_active_time) ||
      (offset_active_time - start_offset == active_duration &&
       specified.iteration_count &&
       EndsOnIterationBoundary(specified.iteration_count,
                               specified.iteration_start)))
    return iteration_duration;

  DCHECK(std::isfinite(offset_active_time));
  double iteration_time = fmod(offset_active_time, iteration_duration);

  // This implements step 3 of
  // https://drafts.csswg.org/web-animations/#calculating-the-simple-iteration-progress
  if (iteration_time == 0 && phase == Timing::kPhaseAfter &&
      active_duration != 0 && offset_active_time != 0)
    return iteration_duration;

  return iteration_time;
}

}  // namespace blink

#endif
