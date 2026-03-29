/// <reference types="vite/client" />

declare module "*.css";

interface ImportMetaEnv {
  readonly VITE_OPERATOR_API_BASE_URL?: string;
  readonly VITE_OPERATOR_DATA_MODE?: "auto" | "live" | "mock";
}
