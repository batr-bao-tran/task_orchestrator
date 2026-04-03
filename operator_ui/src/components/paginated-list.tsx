import { useState, useMemo, type ReactNode } from "react";

const PAGE_SIZE = 10;

interface PaginatedListProps<T> {
  items: T[];
  renderItem: (item: T, index: number) => ReactNode;
  keyExtractor: (item: T, index: number) => string;
  emptyMessage?: string;
  /** When true the list is displayed newest-first (reversed). Defaults to true. */
  newestFirst?: boolean;
}

export function PaginatedList<T>({
  items,
  renderItem,
  keyExtractor,
  emptyMessage = "Nothing to show.",
  newestFirst = true,
}: PaginatedListProps<T>) {
  const [page, setPage] = useState(0);

  const ordered = useMemo(() => (newestFirst ? [...items].reverse() : items), [items, newestFirst]);
  const totalPages = Math.max(1, Math.ceil(ordered.length / PAGE_SIZE));
  const safePage = Math.min(page, totalPages - 1);
  const pageItems = ordered.slice(safePage * PAGE_SIZE, safePage * PAGE_SIZE + PAGE_SIZE);

  if (ordered.length === 0) {
    return <p className="muted">{emptyMessage}</p>;
  }

  return (
    <>
      {pageItems.map((item, i) => (
        <div key={keyExtractor(item, safePage * PAGE_SIZE + i)}>{renderItem(item, safePage * PAGE_SIZE + i)}</div>
      ))}
      {totalPages > 1 && (
        <div className="pager">
          <button
            className="action-button action-button--quiet"
            disabled={safePage === 0}
            onClick={() => { setPage(safePage - 1); }}
          >
            Prev
          </button>
          <span className="pager-label">
            {String(safePage + 1)} / {String(totalPages)}
          </span>
          <button
            className="action-button action-button--quiet"
            disabled={safePage >= totalPages - 1}
            onClick={() => { setPage(safePage + 1); }}
          >
            Next
          </button>
        </div>
      )}
    </>
  );
}
