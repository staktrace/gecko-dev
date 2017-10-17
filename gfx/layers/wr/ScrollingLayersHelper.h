/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_SCROLLINGLAYERSHELPER_H
#define GFX_SCROLLINGLAYERSHELPER_H

#include <unordered_map>

#include "DisplayItemClipChain.h"
#include "mozilla/Attributes.h"

class nsDisplayItem;

namespace mozilla {

struct ActiveScrolledRoot;

namespace wr {
class DisplayListBuilder;
}

namespace layers {

struct FrameMetrics;
class StackingContextHelper;
class WebRenderLayerManager;

class ScrollingLayersHelper
{
public:
  ScrollingLayersHelper();

  void BeginBuild(WebRenderLayerManager* aManager,
                  wr::DisplayListBuilder& aBuilder);
  void EndBuild();

  void BeginList();
  void EndList();

  void BeginItem(nsDisplayItem* aItem,
                 const StackingContextHelper& aStackingContext);
  ~ScrollingLayersHelper();

private:
  typedef std::pair<FrameMetrics::ViewID, Maybe<wr::WrClipId>> ClipAndScroll;

  std::pair<Maybe<FrameMetrics::ViewID>, Maybe<wr::WrClipId>>
  DefineClipChain(nsDisplayItem* aItem,
                  const ActiveScrolledRoot* aAsr,
                  const DisplayItemClipChain* aChain,
                  int32_t aAppUnitsPerDevPixel,
                  const StackingContextHelper& aStackingContext);

  std::pair<Maybe<FrameMetrics::ViewID>, Maybe<wr::WrClipId>>
  RecurseAndDefineClip(nsDisplayItem* aItem,
                       const ActiveScrolledRoot* aAsr,
                       const DisplayItemClipChain* aChain,
                       int32_t aAppUnitsPerDevPixel,
                       const StackingContextHelper& aSc);

  std::pair<Maybe<FrameMetrics::ViewID>, Maybe<wr::WrClipId>>
  RecurseAndDefineAsr(nsDisplayItem* aItem,
                      const ActiveScrolledRoot* aAsr,
                      const DisplayItemClipChain* aChain,
                      int32_t aAppUnitsPerDevPixel,
                      const StackingContextHelper& aSc);

  Maybe<ClipAndScroll> EnclosingClipAndScroll() const;

  // Note: two DisplayItemClipChain* A and B might actually be "equal" (as per
  // DisplayItemClipChain::Equal(A, B)) even though they are not the same pointer
  // (A != B). We override the hash/equals function on the map to ensure these
  // items get collapsed together.
  typedef std::unordered_map<const DisplayItemClipChain*,
                             wr::WrClipId,
                             DisplayItemClipChainHasher,
                             DisplayItemClipChainEqualer> ClipIdMap;

  WebRenderLayerManager* MOZ_NON_OWNING_REF mManager;
  wr::DisplayListBuilder* mBuilder;
  ClipIdMap mCache;

  struct ItemClips {
    ItemClips(const ActiveScrolledRoot* aAsr,
              const DisplayItemClipChain* aChain);

    const ActiveScrolledRoot* mAsr;
    const DisplayItemClipChain* mChain;

    Maybe<FrameMetrics::ViewID> mScrollId;
    Maybe<wr::WrClipId> mClipId;
    Maybe<ClipAndScroll> mClipAndScroll;

    void Apply(wr::DisplayListBuilder* aBuilder);
    void Unapply(wr::DisplayListBuilder* aBuilder);
    bool HasSameInputs(const ItemClips& aOther);
  };

  std::vector<ItemClips> mItemClipStack;
};

} // namespace layers
} // namespace mozilla

#endif
