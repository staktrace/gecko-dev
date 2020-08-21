/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ScrollPositionUpdate_h_
#define mozilla_ScrollPositionUpdate_h_

#include <cstdint>

#include "nsPoint.h"
#include "mozilla/ScrollOrigin.h"
#include "mozilla/ScrollTypes.h"
#include "Units.h"

namespace mozilla {

enum class ScrollUpdateType {
  Absolute,
  Relative,
  PureRelative,
};

class ScrollPositionUpdate {
 public:
  explicit ScrollPositionUpdate();

  static ScrollPositionUpdate NewScroll(uint32_t aGeneration,
                                        ScrollOrigin aOrigin,
                                        nsPoint aDestination);
  static ScrollPositionUpdate NewRelativeScroll(uint32_t aGeneration,
                                                nsPoint aSource,
                                                nsPoint aDestination);
  static ScrollPositionUpdate NewSmoothScroll(uint32_t aGeneration,
                                              ScrollOrigin aOrigin,
                                              nsPoint aDestination);
  static ScrollPositionUpdate NewPureRelativeScroll(uint32_t aGeneration,
                                                    ScrollOrigin aOrigin,
                                                    ScrollMode aMode,
                                                    const nsPoint& aDelta);

  bool operator==(const ScrollPositionUpdate& aOther) const;

  uint32_t GetGeneration() const;
  ScrollUpdateType GetType() const;
  ScrollMode GetMode() const;
  ScrollOrigin GetOrigin() const;
  CSSPoint GetDestination() const;
  CSSPoint GetSource() const;
  CSSPoint GetDelta() const;

 private:
  uint32_t mScrollGeneration;
  ScrollUpdateType mType;
  ScrollMode mScrollMode;
  ScrollOrigin mScrollOrigin;
  // mDestination is only valid for Absolute and Relative types
  CSSPoint mDestination;
  // mSource is only valid for Relative types
  CSSPoint mSource;
  // mDelta is only valid for PureRelative types
  CSSPoint mDelta;
};

}  // namespace mozilla

#endif  // mozilla_ScrollPositionUpdate_h_
