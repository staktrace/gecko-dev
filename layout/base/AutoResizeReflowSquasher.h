/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_AutoResizeReflowSquasher_h_
#define mozilla_AutoResizeReflowSquasher_h_

#include "mozilla/Attributes.h"
#include "mozilla/PresShellForwards.h"

namespace mozilla {

// Put one of these on the stack to "capture" any calls to
// PresShell::ResizeReflow and have them execute when this class is destroyed.
// This effectively "squashes" together multiple calls to ResizeReflow and
// combines them into one call at the end of the scope that contains this
// RAII class. Note that all the calls to ResizeReflow must be on the same
// PresShell and have the same arguments, otherwise an assertion will be
// thrown.
class MOZ_RAII AutoResizeReflowSquasher {
 public:
  AutoResizeReflowSquasher();
  MOZ_CAN_RUN_SCRIPT ~AutoResizeReflowSquasher();
  static bool CaptureResizeReflow(PresShell* aShell, nscoord aWidth,
                                  nscoord aHeight,
                                  ResizeReflowOptions aOptions);

 private:
  static bool sOnStack;
  static StaticRefPtr<PresShell> sPresShell;
  static nscoord sWidth;
  static nscoord sHeight;
  static ResizeReflowOptions sOptions;
};

}  // namespace mozilla

#endif  // mozilla_AutoResizeReflowSquasher_h_
