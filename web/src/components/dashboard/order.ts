// The dashboard's cards in their default order: the roof straight after Status (spec 009).
export const DEFAULT_ORDER = ['status', 'roof', 'device'];
const ORDER_KEY = 'roofroof.dashboard.order';

// Card order is a per-browser preference.
export const loadOrder = (): string[] => {
  try {
    const value = JSON.parse(localStorage.getItem(ORDER_KEY) ?? '[]');
    return Array.isArray(value) ? value.filter((id) => typeof id === 'string') : [];
  } catch {
    return [];
  }
};
export const saveOrder = (order: string[]) => {
  try {
    if (order.length) localStorage.setItem(ORDER_KEY, JSON.stringify(order));
    else localStorage.removeItem(ORDER_KEY);
  } catch {
    // storage unavailable: the order just isn't remembered
  }
};
