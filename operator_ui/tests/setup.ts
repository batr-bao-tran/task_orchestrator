import "@testing-library/jest-dom/vitest";

// jsdom does not implement ResizeObserver
if (!("ResizeObserver" in globalThis)) {
  globalThis.ResizeObserver = class ResizeObserver {
    observe() { /* stub */ }
    unobserve() { /* stub */ }
    disconnect() { /* stub */ }
  } as unknown as typeof globalThis.ResizeObserver;
}
