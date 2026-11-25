// Where a "?" tip goes in the window (spec 028): centred above its "?", below
// it when there is no room above, and always inside the window.

interface Box {
  top: number;
  bottom: number;
  left: number;
  width: number;
}

const GAP = 7; // px between the "?" and its tip
const MARGIN = 8; // px kept clear of the window's edges

export const tipPlace = (anchor: Box, tip: { width: number; height: number }, view: { width: number; height: number }) => {
  const above = anchor.top - GAP - tip.height;
  const top = above >= MARGIN ? above : Math.max(MARGIN, Math.min(anchor.bottom + GAP, view.height - tip.height - MARGIN));
  const centred = anchor.left + anchor.width / 2 - tip.width / 2;
  const left = Math.max(MARGIN, Math.min(centred, view.width - tip.width - MARGIN));
  return { top, left };
};
