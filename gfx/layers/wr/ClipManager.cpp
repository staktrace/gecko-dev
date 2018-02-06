/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/ClipManager.h"

#include "DisplayItemClipChain.h"
#include "FrameMetrics.h"
#include "LayersLogging.h"
#include "mozilla/layers/StackingContextHelper.h"
#include "mozilla/layers/WebRenderLayerManager.h"
#include "mozilla/webrender/WebRenderAPI.h"
#include "nsDisplayList.h"
#include "UnitTransforms.h"

//#define CLIP_LOG(...)
//#define CLIP_LOG(...) printf_stderr("CLIP: " __VA_ARGS__)
#define CLIP_LOG(...) if (XRE_IsContentProcess()) printf_stderr("CLIP: " __VA_ARGS__)

namespace mozilla {
namespace layers {

ClipManager::ClipManager()
  : mManager(nullptr)
  , mBuilder(nullptr)
{
}

void
ClipManager::BeginBuild(WebRenderLayerManager* aManager,
                        wr::DisplayListBuilder& aBuilder)
{
  MOZ_ASSERT(!mManager);
  mManager = aManager;
  MOZ_ASSERT(!mBuilder);
  mBuilder = &aBuilder;
  MOZ_ASSERT(mCacheStack.empty());
  mCacheStack.emplace_back();
  MOZ_ASSERT(mASROverride.empty());
  MOZ_ASSERT(mItemClipStack.empty());
}

void
ClipManager::EndBuild()
{
  mBuilder = nullptr;
  mManager = nullptr;
  mCacheStack.pop_back();
  MOZ_ASSERT(mCacheStack.empty());
  MOZ_ASSERT(mASROverride.empty());
  MOZ_ASSERT(mItemClipStack.empty());
}

void
ClipManager::BeginList(const StackingContextHelper& aStackingContext)
{
  if (aStackingContext.AffectsClipPositioning()) {
    PushOverrideForASR(
        mItemClipStack.empty() ? nullptr : mItemClipStack.back().mASR,
        Nothing());
  }

  ItemClips clips(nullptr, nullptr);
  if (!mItemClipStack.empty()) {
    clips.CopyOutputsFrom(mItemClipStack.back());
  }
  mItemClipStack.push_back(clips);
}

void
ClipManager::EndList(const StackingContextHelper& aStackingContext)
{
  MOZ_ASSERT(!mItemClipStack.empty());
  mItemClipStack.back().Unapply(mBuilder);
  mItemClipStack.pop_back();

  if (aStackingContext.AffectsClipPositioning()) {
    PopOverrideForASR(
        mItemClipStack.empty() ? nullptr : mItemClipStack.back().mASR);
  }
}

void
ClipManager::PushOverrideForASR(const ActiveScrolledRoot* aASR,
                                const Maybe<wr::WrClipId>& aClipId)
{
  layers::FrameMetrics::ViewID viewId = aASR
      ? aASR->GetViewId() : layers::FrameMetrics::NULL_SCROLL_ID;
  Maybe<wr::WrClipId> scrollId = mBuilder->GetScrollIdForDefinedScrollLayer(viewId);
  MOZ_ASSERT(scrollId.isSome());

  CLIP_LOG("Pushing override %" PRIu64 " -> %s\n", scrollId->id,
      aClipId ? Stringify(aClipId->id).c_str() : "(none)");
  auto it = mASROverride.insert({ scrollId->id, std::vector<Maybe<wr::WrClipId>>() });
  it.first->second.push_back(aClipId);

  mCacheStack.emplace_back();
}

void
ClipManager::PopOverrideForASR(const ActiveScrolledRoot* aASR)
{
  MOZ_ASSERT(!mCacheStack.empty());
  mCacheStack.pop_back();

  layers::FrameMetrics::ViewID viewId = aASR
      ? aASR->GetViewId() : layers::FrameMetrics::NULL_SCROLL_ID;
  Maybe<wr::WrClipId> scrollId = mBuilder->GetScrollIdForDefinedScrollLayer(viewId);
  MOZ_ASSERT(scrollId.isSome());

  auto it = mASROverride.find(scrollId->id);
  MOZ_ASSERT(it != mASROverride.end());
  MOZ_ASSERT(!(it->second.empty()));
  CLIP_LOG("Popping override %" PRIu64 " -> %s\n", scrollId->id,
      it->second.back() ? Stringify(it->second.back()->id).c_str() : "(none)");
  it->second.pop_back();
  if (it->second.empty()) {
    mASROverride.erase(it);
  }
}

Maybe<wr::WrClipId>
ClipManager::ClipIdAfterOverride(const Maybe<wr::WrClipId>& aClipId)
{
  if (!aClipId) {
    return Nothing();
  }
  auto it = mASROverride.find(aClipId->id);
  if (it == mASROverride.end()) {
    return aClipId;
  }
  MOZ_ASSERT(!it->second.empty());
  CLIP_LOG("Overriding %" PRIu64 " with %s\n", aClipId->id,
      it->second.back() ? Stringify(it->second.back()->id).c_str() : "(none)");
  return it->second.back();
}

void
ClipManager::BeginItem(nsDisplayItem* aItem,
                       const StackingContextHelper& aStackingContext)
{
  CLIP_LOG("processing item %p\n", aItem);

  const DisplayItemClipChain* clip = aItem->GetClipChain();
  const ActiveScrolledRoot* asr = aItem->GetActiveScrolledRoot();

  ItemClips clips(asr, clip);
  MOZ_ASSERT(!mItemClipStack.empty());
  if (clips.HasSameInputs(mItemClipStack.back())) {
    // Early-exit because if the clips are the same then we don't need to do
    // do the work of popping the old stuff and then pushing it right back on
    // for the new item.
    CLIP_LOG("early-exit for %p\n", aItem);
    return;
  }
  mItemClipStack.back().Unapply(mBuilder);
  mItemClipStack.pop_back();

  // Zoom display items report their bounds etc using the parent document's
  // APD because zoom items act as a conversion layer between the two different
  // APDs.
  int32_t auPerDevPixel = aItem->Frame()->PresContext()->AppUnitsPerDevPixel();
  if (aItem->GetType() == DisplayItemType::TYPE_ZOOM) {
    auPerDevPixel = static_cast<nsDisplayZoom*>(aItem)->GetParentAppUnitsPerDevPixel();
  }

  // There are two ASR chains here that we need to be fully defined. One is the
  // ASR chain pointed to by |asr|. The other is the
  // ASR chain pointed to by clip->mASR. We pick the leafmost
  // of these two chains because that one will include the other. Calling
  // DefineScrollLayers with this leafmost ASR will recursively define all the
  // ASRs that we care about for this item, but will not actually push
  // anything onto the WR stack.
  const ActiveScrolledRoot* leafmostASR = asr;
  if (clip) {
    leafmostASR = ActiveScrolledRoot::PickDescendant(leafmostASR, clip->mASR);
  }
  Maybe<wr::WrClipId> leafmostId = DefineScrollLayers(leafmostASR, aItem, aStackingContext);
  // Define all the clips in the item's clip chain, and obtain a clip chain id
  // for it.
  clips.mClipChainId = DefineClipChain(clip, auPerDevPixel, aStackingContext);

  if (clip && clip->mASR == asr) {
    // For the case where there is a stacking context between the ASR and
    // this item, and we need this item to not get hoisted out to the ASR.
    // If the item has a clip we use that, if not we don't push anything
    // so it implicitly gets attached to the StackingContext.
    const ClipIdMap& cache = mCacheStack.back();
    auto it = cache.find(clip);
    MOZ_ASSERT(it != cache.end());
    clips.mScrollId = Some(it->second);
  } else if (clip) {
    // If we don't have a clip at all then we don't need to push anything
    // for this item because we'll get the ASR for free(?). But if we do
    // push the ASR here then we might inadvertently hoist it out of a
    // stacking context between this item and the ASR.
    FrameMetrics::ViewID viewId = asr
        ? asr->GetViewId()
        : FrameMetrics::NULL_SCROLL_ID;
    Maybe<wr::WrClipId> scrollId =
        mBuilder->GetScrollIdForDefinedScrollLayer(viewId);
    MOZ_ASSERT(scrollId.isSome());
    //scrollId = ClipIdAfterOverride(scrollId);
    clips.mScrollId = scrollId;
  }

  // Now that we have the scroll id and a clip id for the item, push it onto
  // the WR stack.
  clips.Apply(mBuilder);
  mItemClipStack.push_back(clips);

  CLIP_LOG("done setup for %p\n", aItem);
}

Maybe<wr::WrClipId>
ClipManager::DefineScrollLayers(const ActiveScrolledRoot* aASR,
                                nsDisplayItem* aItem,
                                const StackingContextHelper& aSc)
{
  if (!aASR) {
    return Nothing();
  }
  FrameMetrics::ViewID viewId = aASR->GetViewId();
  Maybe<wr::WrClipId> scrollId = mBuilder->GetScrollIdForDefinedScrollLayer(viewId);
  if (scrollId) {
    // If we've already defined this scroll layer before, we can early-exit
    return scrollId;
  }
  // Recurse to define the ancestors
  Maybe<wr::WrClipId> ancestorScrollId = DefineScrollLayers(aASR->mParent, aItem, aSc);

  // Ok to pass nullptr for aLayer here (first arg) because aClip (last arg) is
  // also nullptr.
  Maybe<ScrollMetadata> metadata = aASR->mScrollableFrame->ComputeScrollMetadata(
      nullptr, mManager, aItem->ReferenceFrame(), ContainerLayerParameters(), nullptr);
  MOZ_ASSERT(metadata);
  FrameMetrics& metrics = metadata->GetMetrics();

  if (!metrics.IsScrollable()) {
    // This item is a no-op, skip over it
    return ancestorScrollId;
  }

  LayoutDeviceRect contentRect =
      metrics.GetExpandedScrollableRect() * metrics.GetDevPixelsPerCSSPixel();
  LayoutDeviceRect clipBounds =
      LayoutDeviceRect::FromUnknownRect(metrics.GetCompositionBounds().ToUnknownRect());
  // The content rect that we hand to PushScrollLayer should be relative to
  // the same origin as the clipBounds that we hand to PushScrollLayer - that
  // is, both of them should be relative to the stacking context `aSc`.
  // However, when we get the scrollable rect from the FrameMetrics, the origin
  // has nothing to do with the position of the frame but instead represents
  // the minimum allowed scroll offset of the scrollable content. While APZ
  // uses this to clamp the scroll position, we don't need to send this to
  // WebRender at all. Instead, we take the position from the composition
  // bounds.
  contentRect.MoveTo(clipBounds.TopLeft());

  Maybe<wr::WrClipId> parent = ClipIdAfterOverride(ancestorScrollId);
  scrollId = Some(mBuilder->DefineScrollLayer(viewId, parent,
      aSc.ToRelativeLayoutRect(contentRect),
      aSc.ToRelativeLayoutRect(clipBounds)));

  return scrollId;
}

Maybe<wr::WrClipChainId>
ClipManager::DefineClipChain(const DisplayItemClipChain* aChain,
                             int32_t aAppUnitsPerDevPixel,
                             const StackingContextHelper& aSc)
{
  nsTArray<wr::WrClipId> clipIds;
  for (const DisplayItemClipChain* chain = aChain; chain; chain = chain->mParent) {
    ClipIdMap& cache = mCacheStack.back();
    auto it = cache.find(chain);
    if (it != cache.end()) {
      CLIP_LOG("cache[%p] => %" PRIu64 "\n", chain, it->second.id);
      clipIds.AppendElement(it->second);
      continue;
    }
    if (!chain->mClip.HasClip()) {
      // This item in the chain is a no-op, skip over it
      continue;
    }

    LayoutDeviceRect clip = LayoutDeviceRect::FromAppUnits(
        chain->mClip.GetClipRect(), aAppUnitsPerDevPixel);
    nsTArray<wr::ComplexClipRegion> wrRoundedRects;
    chain->mClip.ToComplexClipRegions(aAppUnitsPerDevPixel, aSc, wrRoundedRects);

    FrameMetrics::ViewID viewId = chain->mASR
        ? chain->mASR->GetViewId()
        : FrameMetrics::NULL_SCROLL_ID;
    Maybe<wr::WrClipId> scrollId =
      mBuilder->GetScrollIdForDefinedScrollLayer(viewId);
    MOZ_ASSERT(scrollId.isSome());

    // Define the clip
    // XXX deal with oob clips between self and ancestor
    Maybe<wr::WrClipId> parent = ClipIdAfterOverride(scrollId);
    wr::WrClipId clipId = mBuilder->DefineClip(
        parent,
        aSc.ToRelativeLayoutRect(clip), &wrRoundedRects);
    clipIds.AppendElement(clipId);
    cache[chain] = clipId;
    CLIP_LOG("cache[%p] <= %" PRIu64 "\n", chain, clipId.id);
  }

  Maybe<wr::WrClipChainId> parentChainId;
  if (!mItemClipStack.empty()) {
    parentChainId = mItemClipStack.back().mClipChainId;
  }

  Maybe<wr::WrClipChainId> chainId;
  if (clipIds.Length() > 0) {
    chainId = Some(mBuilder->DefineClipChain(parentChainId, clipIds));
  } else {
    chainId = parentChainId;
  }
  return chainId;
}

ClipManager::~ClipManager()
{
  MOZ_ASSERT(!mBuilder);
  MOZ_ASSERT(mCacheStack.empty());
  MOZ_ASSERT(mItemClipStack.empty());
}

ClipManager::ItemClips::ItemClips(const ActiveScrolledRoot* aASR,
                                  const DisplayItemClipChain* aChain)
  : mASR(aASR)
  , mChain(aChain)
  , mApplied(false)
{
}

void
ClipManager::ItemClips::Apply(wr::DisplayListBuilder* aBuilder)
{
  MOZ_ASSERT(!mApplied);
  mApplied = true;
  if (mScrollId) {
    aBuilder->PushClipAndScrollInfo(*mScrollId,
                                    mClipChainId.ptrOr(nullptr));
  }
}

void
ClipManager::ItemClips::Unapply(wr::DisplayListBuilder* aBuilder)
{
  if (mApplied) {
    mApplied = false;
    if (mScrollId) {
      aBuilder->PopClipAndScrollInfo();
    }
  }
}

bool
ClipManager::ItemClips::HasSameInputs(const ItemClips& aOther)
{
  return mASR == aOther.mASR &&
         mChain == aOther.mChain;
}

void
ClipManager::ItemClips::CopyOutputsFrom(const ItemClips& aOther)
{
  mScrollId = aOther.mScrollId;
  mClipChainId = aOther.mClipChainId;
}

} // namespace layers
} // namespace mozilla
