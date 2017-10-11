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
  , mPushedClipAndScroll(false)
{
  int32_t auPerDevPixel = aItem->Frame()->PresContext()->AppUnitsPerDevPixel();

  if (!aApzEnabled) {
    // If APZ is not enabled, we can ignore all the stuff with ASRs; we just
    // need to define the clip chain on the item and that's it.
    DefineAndPushChain(aItem->GetClipChain(), aBuilder, aStackingContext,
        auPerDevPixel, aCache);
    return;
  }

  // There are two ASR chains here that we need to be fully defined. One is the
  // ASR chain pointed to by aItem->GetActiveScrolledRoot(). The other is the
  // ASR chain pointed to by aItem->GetClipChain()->mASR. We pick the leafmost
  // of these two chains because that one will include the other. And then we
  // call DefineAndPushScrollLayers with it, which will recursively push all
  // the necessary clips and scroll layer items for that ASR chain.
  const ActiveScrolledRoot* leafmostASR = aItem->GetActiveScrolledRoot();
  if (aItem->GetClipChain()) {
    leafmostASR = ActiveScrolledRoot::PickDescendant(leafmostASR,
        aItem->GetClipChain()->mASR);
  }
  DefineAndPushScrollLayers(aItem, leafmostASR,
      aItem->GetClipChain(), aBuilder, auPerDevPixel, aStackingContext, aCache);

  // Next, we push the leaf part of the clip chain that is scrolled by the
  // leafmost ASR. All the clips outside the leafmost ASR were already pushed
  // in the above call. This call may be a no-op if the item's ASR got picked
  // as the leaftmostASR previously, because that means these clips were pushed
  // already as being "outside" leafmostASR.
  DefineAndPushChain(aItem->GetClipChain(), aBuilder, aStackingContext,
      auPerDevPixel, aCache);

  // Finally, if clip chain's ASR was the leafmost ASR, then the top of the
  // scroll id stack right now will point to that, rather than the item's ASR
  // which is what we want. So we override that by doing a PushClipAndScrollInfo
  // call. This should generally only happen for fixed-pos type items, but we
  // use code generic enough to handle other cases.
  FrameMetrics::ViewID scrollId = aItem->GetActiveScrolledRoot()
      ? nsLayoutUtils::ViewIDForASR(aItem->GetActiveScrolledRoot())
      : FrameMetrics::NULL_SCROLL_ID;
  if (aBuilder.TopmostScrollId() != scrollId) {
    Maybe<wr::WrClipId> clipId = mBuilder->TopmostClipId();
    mBuilder->PushClipAndScrollInfo(scrollId, clipId.ptrOr(nullptr));
    mPushedClipAndScroll = true;
  }
}

std::pair<Maybe<FrameMetrics::ViewID>, Maybe<wr::WrClipId>>
ScrollingLayersHelper::DefineClipChain(nsDisplayItem* aItem,
                                       const ActiveScrolledRoot* aAsr,
                                       const DisplayItemClipChain* aChain,
                                       int32_t aAppUnitsPerDevPixel,
                                       const StackingContextHelper& aStackingContext,
                                       WebRenderCommandBuilder::ClipIdMap& aCache)
{
  std::pair<Maybe<FrameMetrics::ViewID>, Maybe<wr::WrClipId>> ids;

  // aChain is null, aAsr is null     => return
  // aChain is null, aAsr is non-null => recurse(aAsr->mParent, null),
  //                                     define aAsr
  // aChain->mASR != aAsr             => recurse(aAsr->mParent, aChain),
  //                                     define aAsr
  // aChain->mASR == aAsr             => recurse(aAsr, aChain->mParent),
  //                                     define aChain
  // aChain is non-null, aAsr is null => assert(aChain->mASR == null),
  //                                     recurse(null, aChain->mParent),
  //                                     define aChain
  //
  // invariants: PickDescendant(aChain->mASR, aAsr) == aAsr

  if (aChain && aChain->mASR == aAsr) {
    auto it = aCache.find(aChain);
    if (it != aCache.end()) {
      // If we've already defined this clip before, we can early-exit
      if (aAsr) {
        FrameMetrics::ViewID scrollId = nsLayoutUtils::ViewIDForASR(aAsr);
        MOZ_ASSERT(mBuilder->IsScrollLayerDefined(scrollId));
        ids.first = Some(scrollId);
      }
      ids.second = Some(it->second);
      return ids;
    }

    auto ancestorIds = DefineClipChain(
        aItem, aAsr, aChain->mParent, aAppUnitsPerDevPixel, aStackingContext,
        aCache);
    ids = ancestorIds;

    if (!aChain->mClip.HasClip()) {
      // This item in the chain is a no-op, skip over it
      return ids;
    }

    if (aChain->mParent) {
      if (aChain->mParent->mASR == aAsr) {
        // If the parent clip item shares the ASR, then this clip needs to be
        // a child of the parent clip, which will automatically inherit from
        // the ASR.
        ancestorIds.first = Nothing();
      } else {
        // But if the ASRs are different, this is the outermost clip that's
        // still inside aAsr, and we need to make it a child of aAsr rather
        // than aChain->mParent.
        ancestorIds.second = Nothing();
      }
    }
    MOZ_ASSERT(!(ancestorIds.first && ancestorIds.second));

    LayoutDeviceRect clip = LayoutDeviceRect::FromAppUnits(
        aChain->mClip.GetClipRect(), aAppUnitsPerDevPixel);
    nsTArray<wr::ComplexClipRegion> wrRoundedRects;
    aChain->mClip.ToComplexClipRegions(aAppUnitsPerDevPixel, aStackingContext, wrRoundedRects);

    wr::WrClipId clipId = mBuilder->DefineClip(
            ancestorIds.first, ancestorIds.second,
            aStackingContext.ToRelativeLayoutRect(clip), &wrRoundedRects);
    aCache[aChain] = clipId;

    ids.second = Some(clipId);
    return ids;
  }

  if (aAsr) {
    FrameMetrics::ViewID scrollId = nsLayoutUtils::ViewIDForASR(aAsr);
    if (mBuilder->IsScrollLayerDefined(scrollId)) {
      // If we've already defined this scroll layer before, we can early-exit
      ids.first = Some(scrollId);
      if (aChain) {
        MOZ_ASSERT(ActiveScrolledRoot::PickDescendant(aChain->mASR, aAsr) == aAsr);
        auto it = aCache.find(aChain);
        MOZ_ASSERT(it != aCache.end());
        ids.second = Some(it->second);
      }
      return ids;
    }

    auto ancestorIds = DefineClipChain(
        aItem, aAsr->mParent, aChain, aAppUnitsPerDevPixel, aStackingContext,
        aCache);
    ids = ancestorIds;

    Maybe<ScrollMetadata> metadata = aAsr->mScrollableFrame->ComputeScrollMetadata(
        nullptr, aItem->ReferenceFrame(), ContainerLayerParameters(), nullptr);
    MOZ_ASSERT(metadata);
    FrameMetrics& metrics = metadata->GetMetrics();

    if (!metrics.IsScrollable()) {
      return ids;
    }

    if (aChain && ancestorIds.first) {
      if (!aChain->mASR ||
          nsLayoutUtils::ViewIDForASR(aChain->mASR) == ancestorIds.first.value()) {
        ancestorIds.first = Nothing();
      }
    }
    MOZ_ASSERT(!(ancestorIds.first && ancestorIds.second));

    LayerRect contentRect = ViewAs<LayerPixel>(
        metrics.GetExpandedScrollableRect() * metrics.GetDevPixelsPerCSSPixel(),
        PixelCastJustification::WebRenderHasUnitResolution);
    // TODO: check coordinate systems are sane here
    LayerRect clipBounds = ViewAs<LayerPixel>(
        metrics.GetCompositionBounds(),
        PixelCastJustification::MovingDownToChildren);
    // The content rect that we hand to PushScrollLayer should be relative to
    // the same origin as the clipBounds that we hand to PushScrollLayer - that
    // is, both of them should be relative to the stacking context `aStackingContext`.
    // However, when we get the scrollable rect from the FrameMetrics, the origin
    // has nothing to do with the position of the frame but instead represents
    // the minimum allowed scroll offset of the scrollable content. While APZ
    // uses this to clamp the scroll position, we don't need to send this to
    // WebRender at all. Instead, we take the position from the composition
    // bounds.
    contentRect.MoveTo(clipBounds.TopLeft());

    mBuilder->DefineScrollLayer(scrollId, ancestorIds.first, ancestorIds.second,
        aStackingContext.ToRelativeLayoutRect(contentRect),
        aStackingContext.ToRelativeLayoutRect(clipBounds));

    ids.first = Some(scrollId);
    return ids;
  }

  // base case of the recursion
  MOZ_ASSERT(!aChain && !aAsr);
  MOZ_ASSERT(!ids.first && !ids.second);
  return ids;
}
                                       

void
ScrollingLayersHelper::DefineAndPushScrollLayers(nsDisplayItem* aItem,
                                                 const ActiveScrolledRoot* aAsr,
                                                 const DisplayItemClipChain* aChain,
                                                 wr::DisplayListBuilder& aBuilder,
                                                 int32_t aAppUnitsPerDevPixel,
                                                 const StackingContextHelper& aStackingContext,
                                                 WebRenderCommandBuilder::ClipIdMap& aCache)
{
  if (!aAsr) {
    return;
  }
  FrameMetrics::ViewID scrollId = nsLayoutUtils::ViewIDForASR(aAsr);
  if (aBuilder.TopmostScrollId() == scrollId) {
    // it's already been pushed, so we don't need to recurse any further.
    return;
  }

  // Find the first clip up the chain that's "outside" aAsr. Any clips
  // that are "inside" aAsr (i.e. that are scrolled by aAsr) will need to be
  // pushed onto the stack after aAsr has been pushed. On the recursive call
  // we need to skip up the clip chain past these clips.
  const DisplayItemClipChain* asrClippedBy = aChain;
  while (asrClippedBy &&
         ActiveScrolledRoot::PickAncestor(asrClippedBy->mASR, aAsr) == aAsr) {
    asrClippedBy = asrClippedBy->mParent;
  }

  // Recurse up the ASR chain to make sure all ancestor scroll layers and their
  // enclosing clips are defined and pushed onto the WR stack.
  DefineAndPushScrollLayers(aItem, aAsr->mParent, asrClippedBy, aBuilder,
      aAppUnitsPerDevPixel, aStackingContext, aCache);

  // Once the ancestors are dealt with, we want to make sure all the clips
  // enclosing aAsr are pushed. All the clips enclosing aAsr->mParent were
  // already taken care of in the recursive call, so DefineAndPushChain will
  // push exactly what we want.
  DefineAndPushChain(asrClippedBy, aBuilder, aStackingContext,
      aAppUnitsPerDevPixel, aCache);
  // Finally, push the ASR itself as a scroll layer. If it's already defined
  // we can skip the expensive step of computing the ScrollMetadata.
  bool pushed = false;
  if (mBuilder->IsScrollLayerDefined(scrollId)) {
    mBuilder->PushScrollLayer(scrollId);
    pushed = true;
  } else {
    Maybe<ScrollMetadata> metadata = aAsr->mScrollableFrame->ComputeScrollMetadata(
        nullptr, aItem->ReferenceFrame(), ContainerLayerParameters(), nullptr);
    MOZ_ASSERT(metadata);
    pushed = DefineAndPushScrollLayer(metadata->GetMetrics(), aStackingContext);
  }
  if (pushed) {
    mPushedClips.push_back(wr::ScrollOrClipId(scrollId));
  }
}

void
ScrollingLayersHelper::DefineAndPushChain(const DisplayItemClipChain* aChain,
                                          wr::DisplayListBuilder& aBuilder,
                                          const StackingContextHelper& aStackingContext,
                                          int32_t aAppUnitsPerDevPixel,
                                          WebRenderCommandBuilder::ClipIdMap& aCache)
{
  if (!aChain) {
    return;
  }
  auto it = aCache.find(aChain);
  Maybe<wr::WrClipId> clipId = (it != aCache.end() ? Some(it->second) : Nothing());
  if (clipId && clipId == aBuilder.TopmostClipId()) {
    // it was already in the cache and pushed on the WR clip stack, so we don't
    // need to recurse any further.
    return;
  }
  // Recurse up the clip chain to make sure all ancestor clips are defined and
  // pushed onto the WR clip stack. Note that the recursion can invalidate the
  // iterator `it`.
  DefineAndPushChain(aChain->mParent, aBuilder, aStackingContext, aAppUnitsPerDevPixel, aCache);

  if (!aChain->mClip.HasClip()) {
    // This item in the chain is a no-op, skip over it
    return;
  }
  if (!clipId || aBuilder.HasMaskClip()) {
    // If we don't have a clip id for this chain item yet, define the clip in WR
    // and save the id
    LayoutDeviceRect clip = LayoutDeviceRect::FromAppUnits(
        aChain->mClip.GetClipRect(), aAppUnitsPerDevPixel);
    nsTArray<wr::ComplexClipRegion> wrRoundedRects;
    aChain->mClip.ToComplexClipRegions(aAppUnitsPerDevPixel, aStackingContext, wrRoundedRects);
    clipId = Some(aBuilder.DefineClip(Nothing(), Nothing(), aStackingContext.ToRelativeLayoutRect(clip), &wrRoundedRects));
    if (!aBuilder.HasMaskClip()) {
      aCache[aChain] = clipId.value();
    }
  }
  // Finally, push the clip onto the WR stack
  MOZ_ASSERT(clipId);
  aBuilder.PushClip(clipId.value());
  mPushedClips.push_back(wr::ScrollOrClipId(clipId.value()));
}

bool
ScrollingLayersHelper::DefineAndPushScrollLayer(const FrameMetrics& aMetrics,
                                                const StackingContextHelper& aStackingContext)
{
  if (!aMetrics.IsScrollable()) {
    return false;
  }
  LayerRect contentRect = ViewAs<LayerPixel>(
      aMetrics.GetExpandedScrollableRect() * aMetrics.GetDevPixelsPerCSSPixel(),
      PixelCastJustification::WebRenderHasUnitResolution);
  // TODO: check coordinate systems are sane here
  LayerRect clipBounds = ViewAs<LayerPixel>(
      aMetrics.GetCompositionBounds(),
      PixelCastJustification::MovingDownToChildren);
  // The content rect that we hand to PushScrollLayer should be relative to
  // the same origin as the clipBounds that we hand to PushScrollLayer - that
  // is, both of them should be relative to the stacking context `aStackingContext`.
  // However, when we get the scrollable rect from the FrameMetrics, the origin
  // has nothing to do with the position of the frame but instead represents
  // the minimum allowed scroll offset of the scrollable content. While APZ
  // uses this to clamp the scroll position, we don't need to send this to
  // WebRender at all. Instead, we take the position from the composition
  // bounds.
  contentRect.MoveTo(clipBounds.TopLeft());
  mBuilder->DefineScrollLayer(aMetrics.GetScrollId(), Nothing(), Nothing(),
      aStackingContext.ToRelativeLayoutRect(contentRect),
      aStackingContext.ToRelativeLayoutRect(clipBounds));
  mBuilder->PushScrollLayer(aMetrics.GetScrollId());
  return true;
}

ScrollingLayersHelper::~ScrollingLayersHelper()
{
  if (mPushedClipAndScroll) {
    mBuilder->PopClipAndScrollInfo();
  }
  while (!mPushedClips.empty()) {
    wr::ScrollOrClipId id = mPushedClips.back();
    if (id.is<wr::WrClipId>()) {
      mBuilder->PopClip();
    } else {
      MOZ_ASSERT(id.is<FrameMetrics::ViewID>());
      mBuilder->PopScrollLayer();
    }
    mPushedClips.pop_back();
  }
  return;
}

} // namespace layers
} // namespace mozilla
