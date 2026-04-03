import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it } from "vitest";
import { PaginatedList } from "../../src/components/paginated-list";

describe("PaginatedList", () => {
  it("renders an empty message when there are no items", () => {
    render(
      <PaginatedList
        emptyMessage="No history yet."
        items={[] as string[]}
        keyExtractor={(item) => item}
        renderItem={(item) => item}
      />,
    );

    expect(screen.getByText("No history yet.")).toBeInTheDocument();
  });

  it("paginates items newest-first by default", async () => {
    const user = userEvent.setup();
    const items = Array.from({ length: 12 }, (_, index) => `item-${String(index + 1)}`);

    render(
      <PaginatedList
        items={items}
        keyExtractor={(item) => item}
        renderItem={(item, index) => <span>{`${item} @ ${String(index)}`}</span>}
      />,
    );

    expect(screen.getByText("item-12 @ 0")).toBeInTheDocument();
    expect(screen.getByText("item-3 @ 9")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Prev" })).toBeDisabled();
    expect(screen.getByText("1 / 2")).toBeInTheDocument();

    await user.click(screen.getByRole("button", { name: "Next" }));

    expect(screen.getByText("item-2 @ 10")).toBeInTheDocument();
    expect(screen.getByText("item-1 @ 11")).toBeInTheDocument();
    expect(screen.getByText("2 / 2")).toBeInTheDocument();

    await user.click(screen.getByRole("button", { name: "Prev" }));
    expect(screen.getByText("item-12 @ 0")).toBeInTheDocument();
  });

  it("supports oldest-first ordering", () => {
    const items = Array.from({ length: 3 }, (_, index) => `item-${String(index + 1)}`);

    render(
      <PaginatedList
        items={items}
        keyExtractor={(item) => item}
        newestFirst={false}
        renderItem={(item, index) => <span>{`${item} @ ${String(index)}`}</span>}
      />,
    );

    expect(screen.getByText("item-1 @ 0")).toBeInTheDocument();
    expect(screen.getByText("item-3 @ 2")).toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "Next" })).not.toBeInTheDocument();
  });
});
