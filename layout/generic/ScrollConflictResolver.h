/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ScrollConflictResolver_h_
#define mozilla_ScrollConflictResolver_h_

namespace mozilla {

class ScrollConflictResolver {
 private:
  ScrollOrigin mLastScrollOrigin;
};

}  // namespace mozilla

#endif  // mozilla_ScrollConflictResolver_h_
