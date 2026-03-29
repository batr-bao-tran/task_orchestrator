import { render, screen } from "@testing-library/react";
import { describe, expect, it } from "vitest";
import { IntegrationPanel } from "../../src/components/integration-panel";
import { testConnectors } from "../fixtures";

describe("IntegrationPanel", () => {
  it("shows connector targets and the online count", () => {
    render(<IntegrationPanel connectors={testConnectors} />);

    expect(screen.getByText("3 online")).toBeInTheDocument();
    expect(screen.getByText("Warehouse Intake Webhook")).toBeInTheDocument();
    expect(screen.getByText("/connectors/warehouse/orders")).toBeInTheDocument();
    expect(screen.getAllByText("enabled")).toHaveLength(3);
    expect(screen.getByText("disabled")).toBeInTheDocument();
  });

  it("renders an empty state when no connectors are configured", () => {
    render(<IntegrationPanel connectors={[]} />);

    expect(screen.getByText("No connectors are currently registered with the control plane.")).toBeInTheDocument();
  });
});
