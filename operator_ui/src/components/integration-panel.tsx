import type { ConnectorBinding } from "../lib/types";

interface IntegrationPanelProps {
  connectors: ConnectorBinding[];
}

export function IntegrationPanel({ connectors }: IntegrationPanelProps) {
  return (
    <section className="panel">
      <div className="panel-header">
        <div>
          <p className="eyebrow">Integrations</p>
          <h2>Inbound and outbound bindings</h2>
        </div>
        <span className="pill">{connectors.filter((connector) => connector.enabled).length} online</span>
      </div>
      <div className="connector-list">
        {connectors.length === 0 ? (
          <p className="muted">No connectors are currently registered with the control plane.</p>
        ) : (
          connectors.map((connector) => (
            <div className="connector-row" key={connector.id}>
              <div>
                <strong>{connector.displayName}</strong>
                <p>{connector.kind}</p>
              </div>
              <code>{connector.target}</code>
              <span className={`pill ${connector.enabled ? "pill--enabled" : "pill--disabled"}`}>
                {connector.enabled ? "enabled" : "disabled"}
              </span>
            </div>
          ))
        )}
      </div>
    </section>
  );
}
