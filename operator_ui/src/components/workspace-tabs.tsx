import { useState } from "react";

export type WorkspaceTab = "workflows" | "operate" | "simulate" | "history" | "integrations";

interface WorkspaceTabsProps {
  children: (activeTab: WorkspaceTab) => React.ReactNode;
}

const tabs: { id: WorkspaceTab; label: string }[] = [
  { id: "workflows", label: "Workflows" },
  { id: "operate", label: "Operate" },
  { id: "simulate", label: "Simulate" },
  { id: "history", label: "History" },
  { id: "integrations", label: "Integrations" },
];

export function WorkspaceTabs({ children }: WorkspaceTabsProps) {
  const [activeTab, setActiveTab] = useState<WorkspaceTab>("workflows");

  return (
    <div className="workspace">
      <nav className="tab-bar tab-bar--primary" role="tablist">
        {tabs.map((tab) => (
          <button
            aria-selected={activeTab === tab.id}
            className={`tab-button${activeTab === tab.id ? " tab-button--active" : ""}`}
            key={tab.id}
            onClick={() => {
              setActiveTab(tab.id);
            }}
            role="tab"
            type="button"
          >
            {tab.label}
          </button>
        ))}
      </nav>
      {children(activeTab)}
    </div>
  );
}
