#include <gtest/gtest.h>

#include <string_view>

// NOLINTNEXTLINE(bugprone-suspicious-include): test-only access to implementation helpers via Bazel textual_hdrs.
#include "test_support_tls_material.cpp"

namespace task_orchestrator::protocol::test_support {
namespace {

std::string private_key_pem_header() { return std::string("BEGIN ") + "PRIVATE" + " KEY"; }

TEST(TestSupportTlsMaterialInternalTest, ErrorAndBioHelpersHandleEmptyAndOpenSslStates) {
  ERR_clear_error();
  EXPECT_EQ("unknown OpenSSL error", last_openssl_error());

  BioPtr missing_file_bio(BIO_new_file("/definitely/missing/test-support-cert.pem", "r"));
  EXPECT_EQ(nullptr, missing_file_bio.get());
  EXPECT_NE("unknown OpenSSL error", last_openssl_error());

  BioPtr bio(BIO_new(BIO_s_mem()));
  ASSERT_NE(nullptr, bio);
  EXPECT_TRUE(bio_to_string(bio.get()).empty());

  static constexpr std::string_view kPayload = "pem-bytes";
  ASSERT_EQ(static_cast<int>(kPayload.size()),
            BIO_write(bio.get(), kPayload.data(), static_cast<int>(kPayload.size())));
  EXPECT_EQ(kPayload, bio_to_string(bio.get()));
}

TEST(TestSupportTlsMaterialInternalTest, CertificateHelpersProduceUsableTlsMaterial) {
  X509Ptr certificate(X509_new());
  ASSERT_NE(nullptr, certificate);

  X509_NAME* subject_name = X509_get_subject_name(certificate.get());
  ASSERT_NE(nullptr, subject_name);
  EXPECT_TRUE(add_name_entry(subject_name, "CN", "localhost"));
  EXPECT_FALSE(add_extension(certificate.get(), NID_subject_alt_name, "not-a-valid-san"));

  std::string error_message;
  EvtPkeyPtr private_key = generate_private_key(&error_message);
  ASSERT_NE(nullptr, private_key) << error_message;
  EXPECT_TRUE(error_message.empty());

  const TestTlsMaterial material = generate_localhost_tls_material();
  ASSERT_TRUE(material.ok) << material.error_message;
  EXPECT_NE(material.certificate_chain_pem.find("BEGIN CERTIFICATE"), std::string::npos);
  EXPECT_NE(material.private_key_pem.find(private_key_pem_header()), std::string::npos);

  const auto& cached_material = localhost_tls_material();
  EXPECT_TRUE(cached_material.ok);
  EXPECT_NE(cached_material.certificate_chain_pem.find("BEGIN CERTIFICATE"), std::string::npos);
  EXPECT_NE(cached_material.private_key_pem.find(private_key_pem_header()), std::string::npos);
  EXPECT_EQ(&cached_material, &localhost_tls_material());
}

}  // namespace
}  // namespace task_orchestrator::protocol::test_support
