/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/ScrollingLayersHelper.h"

#include "FrameMetrics.h"
#include "mozilla/layers/StackingContextHelper.h"
#include "mozilla/webrender/WebRenderAPI.h"
#include "UnitTransforms.h"

namespace mozilla {
namespace layers {

ScrollingLayersHelper::ScrollingLayersHelper(nsDisplayItem* aItem,
                                             wr::DisplayListBuilder& aBuilder,
                                             const StackingContextHelper& aStackingContext,
                                             WebRenderCommandBuilder::ClipIdMap& aCache,
                                             bool aApzEnabled)
  : mBuilder(&aBuilder)
{
  int32_t auPerDevPixel = aItem->Frame()->PresContext()->AppUnitsPerDevPixel();

  const ActiveScrolledRoot* leafmostASR = aItem->GetActiveScrolledRoot();
  if (aItem->GetClipChain()) {
    leafmostASR = ActiveScrolledRoot::PickDescendant(leafmostASR,
        aItem->GetClipChain()->mASR);
  }
  Maybe<FrameMetrics::ViewID> scrollId = DefineScrollChain(aItem,
          leafmostASR, aStackingContext);
  Maybe<wr::WrClipId> clipId = DefineClipChain(aItem,
          aItem->GetClipChain(), auPerDevPixel, aStackingContext, aCache);

  mBuilder->PushClipAndScrollInfo(scrollId.valueOr(FrameMetrics::NULL_SCROLL_ID),
                                  clipId.ptrOr(nullptr));
}

Maybe<FrameMetrics::ViewID>
ScrollingLayersHelper::DefineScrollChain(nsDisplayItem* aItem,
                                         const ActiveScrolledRoot* aAsr,
                                         const StackingContextHelper& aStackingContext)
{
  if (!aAsr) {
    return Some(FrameMetrics::NULL_SCROLL_ID);
  }
  FrameMetrics::ViewID scrollId = nsLayoutUtils::ViewIDForASR(aAsr);
  if (mBuilder->IsScrollLayerDefined(scrollId)) {
    return Some(scrollId);
  }

  // Recurse to define the parent chain
  Maybe<FrameMetrics::ViewID> parentId = DefineScrollChain(aItem,
          aAsr->mParent, aStackingContext);

  Maybe<ScrollMetadata> metadata = aAsr->mScrollableFrame->ComputeScrollMetadata(
      nullptr, aItem->ReferenceFrame(), ContainerLayerParameters(), nullptr);
  MOZ_ASSERT(metadata);
  FrameMetrics& metrics = metadata->GetMetrics();
  if (!metrics.IsScrollable()) {
    // If this metadata is not scrollable, skip over it in the chain
    return parentId;
  }

  // If we get here, we need to define the scrolling clip because it hasn't
  // been defined yet.
  LayerRect contentRect = ViewAs<LayerPixel>(
      metrics.GetExpandedScrollableRect() * metrics.GetDevPixelsPerCSSPixel(),
      PixelCastJustification::WebRenderHasUnitResolution);
  // TODO: check coordinate systems are sane here
  LayerRect clipBounds = ViewAs<LayerPixel>(
      metrics.GetCompositionBounds(),
      PixelCastJustification::MovingDownToChildren);
  // The content rect that we hand to DefineScrollLayer should be relative to
  // the same origin as the clipBounds that we hand to DefineScrollLayer - that
  // is, both of them should be relative to the stacking context `aStackingContext`.
  // However, when we get the scrollable rect from the FrameMetrics, the origin
  // has nothing to do with the position of the frame but instead represents
  // the minimum allowed scroll offset of the scrollable content. While APZ
  // uses this to clamp the scroll position, we don't need to send this to
  // WebRender at all. Instead, we take the position from the composition
  // bounds.
  contentRect.MoveTo(clipBounds.TopLeft());
  mBuilder->DefineScrollLayer(scrollId, parentId,
      aStackingContext.ToRelativeLayoutRect(contentRect),
      aStackingContext.ToRelativeLayoutRect(clipBounds));
  return Some(scrollId);
}

Maybe<wr::WrClipId>
ScrollingLayersHelper::DefineClipChain(nsDisplayItem* aItem,
                                       const DisplayItemClipChain* aChain,
                                       int32_t aAppUnitsPerDevPixel,
                                       const StackingContextHelper& aStackingContext,
                                       WebRenderCommandBuilder::ClipIdMap& aCache)
{
  if (!aChain) {
    return Nothing();
  }

  if (mBuilder->HasMaskClip()) {
    Maybe<wr::WrClipId> overrideId = mBuilder->GetCacheOverride(aChain);
    if (overrideId) {
      return overrideId;
    }
  } else {
    auto it = aCache.find(aChain);
    if (it != aCache.end()) {
      return Some(it->second);
    }
  }

  Maybe<wr::WrClipId> parentId = DefineClipChain(aItem, aChain->mParent,
          aAppUnitsPerDevPixel, aStackingContext, aCache);

  if (!aChain->mClip.HasClip()) {
    // This item in the chain is a no-op, skip over it
    return parentId;
  }

  // This call should not actually define any new scroll layers since we
  // will have already defined them when we called DefineScrollChain on the
  // leafmost ASR in the ScrollingLayersHelper constructor. We're just using
  // this to get the scrollId.
  Maybe<FrameMetrics::ViewID> scrollId = DefineScrollChain(aItem,
      aChain->mASR, aStackingContext);

  LayoutDeviceRect clip = LayoutDeviceRect::FromAppUnits(
      aChain->mClip.GetClipRect(), aAppUnitsPerDevPixel);
  nsTArray<wr::ComplexClipRegion> wrRoundedRects;
  aChain->mClip.ToComplexClipRegions(aAppUnitsPerDevPixel, aStackingContext, wrRoundedRects);

  wr::WrClipId clipId = mBuilder->DefineClip(scrollId, parentId,
          aStackingContext.ToRelativeLayoutRect(clip), &wrRoundedRects);

  if (!mBuilder->HasMaskClip()) {
    aCache[aChain] = clipId;
  }
  return Some(clipId);
}


ScrollingLayersHelper::~ScrollingLayersHelper()
{
  mBuilder->PopClipAndScrollInfo();
}

} // namespace layers
} // namespace mozilla
