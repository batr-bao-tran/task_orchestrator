#include "protocol/tls_credentials.hpp"

#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>

namespace task_orchestrator::protocol {
namespace {

bool load_data_source(const TlsDataSource& source,
                      const std::string_view label,
                      std::string* loaded_data,
                      std::string* error_message) {
  switch (source.kind) {
    case TlsDataSourceKind::None:
      loaded_data->clear();
      return true;
    case TlsDataSourceKind::FilePath: {
      if (source.value.empty()) {
        *error_message = std::string(label) + " file path is empty.";
        return false;
      }
      std::ifstream file(source.value, std::ios::binary);
      if (!file) {
        *error_message = std::string("Failed to read ") + std::string(label) + " from '" + source.value + "'.";
        return false;
      }
      *loaded_data = std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
      return true;
    }
    case TlsDataSourceKind::InlinePem:
      if (source.value.empty()) {
        *error_message = std::string(label) + " inline PEM is empty.";
        return false;
      }
      *loaded_data = source.value;
      return true;
  }
  *error_message = "Unsupported TLS data source kind.";
  return false;
}

bool load_identity(const TlsIdentityConfig& config,
                   const bool required,
                   const std::string_view owner_label,
                   LoadedTlsIdentityConfig* identity,
                   std::string* error_message) {
  if (!load_data_source(config.certificate_chain,
                        std::string(owner_label) + " certificate chain",
                        &identity->certificate_chain_pem,
                        error_message) ||
      !load_data_source(
          config.private_key, std::string(owner_label) + " private key", &identity->private_key_pem, error_message) ||
      !load_data_source(config.private_key_password,
                        std::string(owner_label) + " private key password",
                        &identity->private_key_password,
                        error_message)) {
    return false;
  }

  const bool has_certificate_chain = !identity->certificate_chain_pem.empty();
  const bool has_private_key = !identity->private_key_pem.empty();
  if (required && (!has_certificate_chain || !has_private_key)) {
    *error_message = std::string(owner_label) + " TLS identity requires both certificate_chain and private_key.";
    return false;
  }
  if (has_certificate_chain != has_private_key) {
    *error_message =
        std::string(owner_label) + " TLS identity must configure certificate_chain and private_key together.";
    return false;
  }
  if (!has_private_key && !identity->private_key_password.empty()) {
    *error_message = std::string(owner_label) + " TLS private_key_password requires a private_key.";
    return false;
  }
  return true;
}

bool load_trust(const TlsTrustConfig& config,
                const std::string_view owner_label,
                const bool require_verification_material,
                LoadedTlsTrustConfig* trust,
                std::string* error_message) {
  trust->use_system_default_roots = config.use_system_default_roots;
  trust->verify_peer = config.verify_peer;
  trust->expected_peer_name = config.expected_peer_name;
  if (!load_data_source(config.root_certificates,
                        std::string(owner_label) + " root certificates",
                        &trust->root_certificates_pem,
                        error_message)) {
    return false;
  }

  if (require_verification_material && trust->verify_peer && !trust->use_system_default_roots &&
      trust->root_certificates_pem.empty()) {
    *error_message =
        std::string(owner_label) + " peer verification requires root_certificates or system default roots.";
    return false;
  }
  return true;
}

}  // namespace

TlsServerLoadResult StaticTlsCredentialProvider::load_server_credentials(const TlsServerConfig& config) const {
  TlsServerLoadResult result;
  result.value.require_client_certificate = config.require_client_certificate;
  if (!load_identity(config.identity, true, "Server", &result.value.identity, &result.error_message) ||
      !load_trust(config.client_trust,
                  "Server",
                  config.require_client_certificate,
                  &result.value.client_trust,
                  &result.error_message)) {
    return result;
  }

  if (result.value.require_client_certificate && !result.value.client_trust.use_system_default_roots &&
      result.value.client_trust.root_certificates_pem.empty()) {
    result.error_message = "Mutual TLS requires client trust roots or system default roots.";
  }
  return result;
}

TlsClientLoadResult StaticTlsCredentialProvider::load_client_credentials(const TlsClientConfig& config) const {
  TlsClientLoadResult result;
  if (!load_identity(config.identity, false, "Client", &result.value.identity, &result.error_message) ||
      !load_trust(config.server_trust, "Client", true, &result.value.server_trust, &result.error_message)) {
    return result;
  }
  return result;
}

std::shared_ptr<const TlsCredentialProvider> make_default_tls_credential_provider() {
  static const auto kProvider = std::make_shared<const StaticTlsCredentialProvider>();
  return kProvider;
}

}  // namespace task_orchestrator::protocol
