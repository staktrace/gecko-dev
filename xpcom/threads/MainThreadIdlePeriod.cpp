/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MainThreadIdlePeriod.h"

#include "mozilla/Maybe.h"
#include "mozilla/Preferences.h"
#include "nsRefreshDriver.h"
#include "nsThreadUtils.h"

// The amount of idle time (milliseconds) reserved for a long idle period.
static const double kLongIdlePeriodMS = 50.0;

// The minimum amount of time (milliseconds) required for an idle period to be
// scheduled on the main thread. N.B. layout.idle_period.time_limit adds
// padding at the end of the idle period, which makes the point in time that we
// expect to become busy again be:
//   now + kMinIdlePeriodMS + layout.idle_period.time_limit
static const double kMinIdlePeriodMS = 3.0;

static const uint32_t kMaxTimerThreadBound = 5;       // milliseconds
static const uint32_t kMaxTimerThreadBoundClamp = 15; // milliseconds

namespace mozilla {

NS_IMETHODIMP
MainThreadIdlePeriod::GetIdlePeriodHint(TimeStamp* aIdleDeadline)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aIdleDeadline);

  TimeStamp now = TimeStamp::Now();
  TimeStamp currentGuess =
    now + TimeDuration::FromMilliseconds(kLongIdlePeriodMS);

  TimeStamp rdCurrentGuess = nsRefreshDriver::GetIdleDeadlineHint(currentGuess);
  currentGuess = NS_GetTimerDeadlineHintOnCurrentThread(rdCurrentGuess, kMaxTimerThreadBound);

  // If the idle period is too small, then just return a null time
  // to indicate we are busy. Otherwise return the actual deadline.
  TimeDuration minIdlePeriod =
    TimeDuration::FromMilliseconds(kMinIdlePeriodMS);
  bool busySoon = currentGuess.IsNull() ||
                  (now >= (currentGuess - minIdlePeriod)) ||
                  currentGuess < mLastIdleDeadline;
if (XRE_IsParentProcess() && busySoon) printf_stderr("busysoon: no guess: %d, next tick in %fms, timer deadline in %fms %d\n",
  currentGuess.IsNull(), (rdCurrentGuess - now).ToMilliseconds(),
  (currentGuess - now).ToMilliseconds(), currentGuess < mLastIdleDeadline);

  if (!busySoon) {
    *aIdleDeadline = mLastIdleDeadline = currentGuess;
  }

  return NS_OK;
}

/* static */ float
MainThreadIdlePeriod::GetLongIdlePeriod()
{
  return kLongIdlePeriodMS;
}

} // namespace mozilla
