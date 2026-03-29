#include "protocol/tls_credentials.hpp"

#include <gtest/gtest.h>

namespace {
namespace tp = task_orchestrator::protocol;

TEST(TlsCredentialsTest, LoadsServerCredentialsFromInlinePemSources) {
  const tp::StaticTlsCredentialProvider provider;
  const tp::TlsServerConfig config{
      .identity =
          {
              .certificate_chain = tp::TlsDataSource::from_inline_pem("cert-pem"),
              .private_key = tp::TlsDataSource::from_inline_pem("key-pem"),
              .private_key_password = tp::TlsDataSource::from_inline_pem("password"),
          },
      .client_trust =
          {
              .root_certificates = tp::TlsDataSource::from_inline_pem("root-pem"),
              .use_system_default_roots = false,
              .verify_peer = true,
              .expected_peer_name = {},
          },
      .require_client_certificate = true,
  };

  const tp::TlsServerLoadResult result = provider.load_server_credentials(config);
  ASSERT_TRUE(result.ok()) << result.error_message;
  EXPECT_EQ("cert-pem", result.value.identity.certificate_chain_pem);
  EXPECT_EQ("key-pem", result.value.identity.private_key_pem);
  EXPECT_EQ("password", result.value.identity.private_key_password);
  EXPECT_EQ("root-pem", result.value.client_trust.root_certificates_pem);
  EXPECT_TRUE(result.value.require_client_certificate);
}

TEST(TlsCredentialsTest, LoadsClientCredentialsFromInlinePemSources) {
  const tp::StaticTlsCredentialProvider provider;
  const tp::TlsClientConfig config{
      .identity =
          {
              .certificate_chain = tp::TlsDataSource::from_inline_pem("client-cert"),
              .private_key = tp::TlsDataSource::from_inline_pem("client-key"),
              .private_key_password = {},
          },
      .server_trust =
          {
              .root_certificates = tp::TlsDataSource::from_inline_pem("server-root"),
              .use_system_default_roots = false,
              .verify_peer = true,
              .expected_peer_name = "planner.internal",
          },
  };

  const tp::TlsClientLoadResult result = provider.load_client_credentials(config);
  ASSERT_TRUE(result.ok()) << result.error_message;
  EXPECT_EQ("client-cert", result.value.identity.certificate_chain_pem);
  EXPECT_EQ("client-key", result.value.identity.private_key_pem);
  EXPECT_EQ("server-root", result.value.server_trust.root_certificates_pem);
  EXPECT_EQ("planner.internal", result.value.server_trust.expected_peer_name);
}

}  // namespace
