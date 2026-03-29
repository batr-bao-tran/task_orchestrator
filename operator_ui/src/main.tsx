import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { App } from "./app";
import "./styles.css";

const rootElement = document.getElementById("root");

if (rootElement === null) {
  throw new Error("Expected #root element to exist before mounting the operator UI.");
}

createRoot(rootElement).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
