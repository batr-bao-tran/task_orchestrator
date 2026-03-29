#ifndef TASK_ORCHESTRATOR__PROTOCOL_INCLUDE_PROTOCOL__TLS_CREDENTIALS_HPP_
#define TASK_ORCHESTRATOR__PROTOCOL_INCLUDE_PROTOCOL__TLS_CREDENTIALS_HPP_

#include <memory>
#include <string>

#include "protocol/runtime_api.hpp"

namespace task_orchestrator::protocol {

/** @brief TLS identity used by transport implementations. */
struct LoadedTlsIdentityConfig {
  std::string certificate_chain_pem;
  std::string private_key_pem;
  std::string private_key_password;

  [[nodiscard]] bool configured() const {
    return !certificate_chain_pem.empty() || !private_key_pem.empty() || !private_key_password.empty();
  }
};

/** @brief TLS trust bundle used by transport implementations. */
struct LoadedTlsTrustConfig {
  std::string root_certificates_pem;
  bool use_system_default_roots = true;
  bool verify_peer = true;
  std::string expected_peer_name;
};

/** @brief Server-side TLS configuration. */
struct LoadedTlsServerConfig {
  LoadedTlsIdentityConfig identity;
  LoadedTlsTrustConfig client_trust;
  bool require_client_certificate = false;
};

/** @brief Client-side TLS configuration. */
struct LoadedTlsClientConfig {
  LoadedTlsIdentityConfig identity;
  LoadedTlsTrustConfig server_trust;
};

template <typename Config>
struct TlsLoadResult {
  Config value{};
  std::string error_message;

  [[nodiscard]] bool ok() const noexcept { return error_message.empty(); }
};

using TlsServerLoadResult = TlsLoadResult<LoadedTlsServerConfig>;
using TlsClientLoadResult = TlsLoadResult<LoadedTlsClientConfig>;

/** @brief Loads TLS credentials from files, inline PEM, or external secret stores. */
class TlsCredentialProvider {
 public:
  virtual ~TlsCredentialProvider() noexcept = default;

  /** @brief Load and validate server TLS materials. */
  virtual TlsServerLoadResult load_server_credentials(const TlsServerConfig& config) const = 0;
  /** @brief Load and validate client TLS materials. */
  virtual TlsClientLoadResult load_client_credentials(const TlsClientConfig& config) const = 0;
};

/** @brief Default provider that reads TLS materials from configured sources. */
class StaticTlsCredentialProvider final : public TlsCredentialProvider {
 public:
  /** @brief Load server credentials from file or inline PEM sources. */
  TlsServerLoadResult load_server_credentials(const TlsServerConfig& config) const override;
  /** @brief Load client credentials from file or inline PEM sources. */
  TlsClientLoadResult load_client_credentials(const TlsClientConfig& config) const override;
};

/** @brief Build the default TLS credential provider for transport instances. */
std::shared_ptr<const TlsCredentialProvider> make_default_tls_credential_provider();

}  // namespace task_orchestrator::protocol

#endif  // TASK_ORCHESTRATOR__PROTOCOL_INCLUDE_PROTOCOL__TLS_CREDENTIALS_HPP_
