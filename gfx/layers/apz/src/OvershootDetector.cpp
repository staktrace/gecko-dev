/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "OvershootDetector.h"

#include "InputData.h"
#include "mozilla/Telemetry.h"  // for Telemetry

namespace mozilla {
namespace layers {

const TimeDuration kOvershootInterval = TimeDuration::FromMilliseconds(100);

static Maybe<gfx::Side> GetScrollDirection(const ScrollWheelInput& aInput) {
  // If the wheel input is scrolling on both axes, we just take the y-axis.
  // This is mostly out of laziness, because this code is for experiment purposes
  // and just needs to handle the cases that will dominate in the data.
  if (aInput.mDeltaY > 0) {
    return Some(gfx::Side::eDown);
  } else if (aInput.mDeltaY < 0) {
    return Some(gfx::Side::eUp);
  } else if (aInput.mDeltaX > 0) {
    return Some(gfx::Side::eRight);
  } else if (aInput.mDeltaX < 0) {
    return Some(gfx::Side::eLeft);
  }
  return Nothing();
}

void OvershootDetector::Update(const ScrollWheelInput& aInput) {
  TimeStamp inputTime = aInput.mTimeStamp;
  Maybe<gfx::Side> inputDirection = GetScrollDirection(aInput);
  if (mLastTimestamp &&
      (inputTime - mLastTimeStamp) > kOvershootInterval &&
      mLastDirection && inputDirection) {
    // The new input we got happened within the kOvershootInterval of the last
    // input. Let's see if the direction is reversed; if so, we accumulate to
    // telemetry.
    bool reversed = false;
    switch (*mLastDirection) {
      case gfx::Side::eDown:
        reversed = (*inputDirection == gfx::Side::eUp);
        break;
      case gfx::Side::eUp:
        reversed = (*inputDirection == gfx::Side::eDown);
        break;
      case gfx::Side::eRight:
        reversed = (*inputDirection == gfx::Side::eLeft);
        break;
      case gfx::Side::eLeft:
        reversed = (*inputDirection == gfx::Side::eRight);
        break;
    }
    if (reversed) {
      Telemetry::Accumulate(Telemetry::APZ_SCROLLWHEEL_OVERSHOOT, 1);
    }
  }

  mLastTimeStamp = inputTime;
  mLastDirection = inputDirection;
}

}  // namespace layers
}  // namespace mozilla
