import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { App } from "../src/app";

describe("App", () => {
  beforeEach(() => {
    vi.stubEnv("VITE_OPERATOR_DATA_MODE", "mock");
  });

  afterEach(() => {
    vi.unstubAllEnvs();
  });

  it("renders the operator dashboard and lets users inspect another workflow", async () => {
    const user = userEvent.setup();

    render(<App />);

    expect(
      await screen.findByRole("heading", {
        name: "Manage your workflows in real time.",
      }),
    ).toBeInTheDocument();
    expect(screen.getByText("events in the last 24h")).toBeInTheDocument();
    expect(screen.getByText("plan versions retained")).toBeInTheDocument();
    expect(screen.getByText("active workflows")).toBeInTheDocument();
    expect(
      await screen.findByText("Dock 2 is congested. Freeze assignments to robot_2 until the pallet backlog clears."),
    ).toBeInTheDocument();

    await user.click(screen.getByRole("button", { name: /shift-capacity-breach/i }));

    expect(await screen.findByRole("heading", { name: "shift-capacity-breach" })).toBeInTheDocument();
    expect(screen.getByText("Investigate worker pool saturation and retry with a higher overtime budget.")).toBeInTheDocument();
    expect(screen.getByText("No assignment diff recorded for this version transition.")).toBeInTheDocument();

    await user.click(screen.getByRole("tab", { name: "Operate" }));
    expect(screen.getByRole("button", { name: "Pause workflow" })).toBeDisabled();
  });

  it("shows integration connectors on the Integrations tab", async () => {
    const user = userEvent.setup();

    render(<App />);

    await screen.findByRole("heading", { name: "Manage your workflows in real time." });

    await user.click(screen.getByRole("tab", { name: "Integrations" }));
    expect(await screen.findByText("Warehouse Intake Webhook")).toBeInTheDocument();
    expect(screen.getByText("disabled")).toBeInTheDocument();
  });

  it("lets operators search the workflow list in mock mode", async () => {
    const user = userEvent.setup();

    render(<App />);

    await screen.findByRole("button", { name: /warehouse-morning-wave/i });
    await user.type(screen.getAllByLabelText("Search workflows")[0], "robot-recovery");

    expect(screen.getByRole("button", { name: /robot-recovery-demo/i })).toBeInTheDocument();
  });
});
