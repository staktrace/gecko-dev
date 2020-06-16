/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AutoResizeReflowSquasher.h"

namespace mozilla {

bool AutoResizeReflowSquasher::sOnStack = false;
StaticRefPtr<PresShell> AutoResizeReflowSquasher::sPresShell;
nscoord AutoResizeReflowSquasher::sWidth = 0;
nscoord AutoResizeReflowSquasher::sHeight = 0;
ResizeReflowOptions AutoResizeReflowSquasher::sOptions =
    ResizeReflowOptions::NoOption;

AutoResizeReflowSquasher::AutoResizeReflowSquasher() {
  // Don't allow nested AutoResizeReflowSquashers
  MOZ_ASSERT(!sOnStack);
  MOZ_ASSERT(!sPresShell);
  sOnStack = true;
}

AutoResizeReflowSquasher::~AutoResizeReflowSquasher() {
  // Make sure to clear sOnStack before calling ResizeReflow, or we will just
  // end up capturing the "squashed" call.
  MOZ_ASSERT(sOnStack);
  sOnStack = false;

  if (sPresShell) {
    // Use a local RefPtr to work around bug 1646129
    RefPtr<PresShell> presShell = sPresShell;
    presShell->ResizeReflow(sWidth, sHeight, sOptions);
    sPresShell = nullptr;
  }
}

bool AutoResizeReflowSquasher::CaptureResizeReflow(
    PresShell* aShell, nscoord aWidth, nscoord aHeight,
    ResizeReflowOptions aOptions) {
  if (!sOnStack) {
    return false;
  }
  if (!sPresShell) {
    sPresShell = aShell;
    sWidth = aWidth;
    sHeight = aHeight;
    sOptions = aOptions;
    return true;
  }

  MOZ_ASSERT(sPresShell.get() == aShell);
  MOZ_ASSERT(sWidth == aWidth);
  MOZ_ASSERT(sHeight == aHeight);
  MOZ_ASSERT(sOptions == aOptions);
  return true;
}

}  // namespace mozilla
