import { defineConfig, loadEnv } from "vite";
import react from "@vitejs/plugin-react";

export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), "");
  const proxyTarget =
    (typeof env.VITE_OPERATOR_API_BASE_URL === "string" && env.VITE_OPERATOR_API_BASE_URL.length > 0
      ? env.VITE_OPERATOR_API_BASE_URL.trim()
      : "") ||
    "http://127.0.0.1:8080";
  const apiKey =
    (typeof env.VITE_OPERATOR_API_KEY === "string" && env.VITE_OPERATOR_API_KEY.length > 0
      ? env.VITE_OPERATOR_API_KEY.trim()
      : "") ||
    "local-dev-key";

  return {
    plugins: [react()],
    server: {
      port: 5173,
      proxy: {
        "/v1": {
          target: proxyTarget,
          changeOrigin: true,
          headers: {
            "x-api-key": apiKey,
          },
        },
      },
    },
  };
});
