import react from "@vitejs/plugin-react";
import { loadEnv } from "vite";
import { defineConfig, mergeConfig } from "vitest/config";

const coverageThreshold = Number(process.env.COVERAGE_THRESHOLD ?? "95");
const branchCoverageThreshold = Number(process.env.BRANCH_COVERAGE_THRESHOLD ?? "80");
const env = loadEnv(process.env.NODE_ENV ?? "test", process.cwd(), "");
const proxyTarget =
  (typeof env.VITE_OPERATOR_API_BASE_URL === "string" && env.VITE_OPERATOR_API_BASE_URL.length > 0
    ? env.VITE_OPERATOR_API_BASE_URL.trim()
    : "") ||
  "http://127.0.0.1:8080";

export default mergeConfig(
  {
    plugins: [react()],
    server: {
      port: 5173,
      proxy: {
        "/v1": {
          target: proxyTarget,
          changeOrigin: true,
        },
      },
    },
  },
  defineConfig({
    test: {
      environment: "jsdom",
      globals: true,
      include: ["tests/**/*.{test,spec}.{ts,tsx}"],
      setupFiles: "./tests/setup.ts",
      coverage: {
        all: true,
        provider: "v8",
        reporter: ["text", "html", "json-summary", "lcov"],
        include: ["src/**/*.{ts,tsx}"],
        exclude: ["src/main.tsx", "src/vite-env.d.ts", "src/lib/types.ts", "tests/**"],
        thresholds: {
          branches: branchCoverageThreshold,
          functions: coverageThreshold,
          lines: coverageThreshold,
          statements: coverageThreshold,
        },
      },
    },
  }),
);
