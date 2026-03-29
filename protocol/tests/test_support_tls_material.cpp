#include "test_support_tls_material.hpp"

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <array>
#include <memory>

namespace task_orchestrator::protocol::test_support {

namespace {

inline constexpr int kX509Version = 2;
inline constexpr long kCertificateSerialNumber = 1;
inline constexpr long kCertificateValiditySeconds = 60L * 60L * 24L * 365L;
inline constexpr int kRsaKeyBits = 2048;
inline constexpr const char* const kCommonName = "localhost";
inline constexpr const char* const kOrganizationName = "Task Orchestrator Tests";
inline constexpr const char* const kSubjectAltName = "DNS:localhost,IP:127.0.0.1";
inline constexpr const char* const kBasicConstraints = "critical,CA:TRUE";
inline constexpr const char* const kKeyUsage = "critical,digitalSignature,keyEncipherment,keyCertSign";
inline constexpr const char* const kExtendedKeyUsage = "serverAuth,clientAuth";

template <typename OpenSslType, auto FreeFn>
struct OpenSslDeleter {
  void operator()(OpenSslType* value) const noexcept {
    if (value != nullptr) {
      static_cast<void>(FreeFn(value));
    }
  }
};

using BioPtr = std::unique_ptr<BIO, OpenSslDeleter<BIO, BIO_free>>;
using EvtPkeyPtr = std::unique_ptr<EVP_PKEY, OpenSslDeleter<EVP_PKEY, EVP_PKEY_free>>;
using EvtPkeyCtxPtr = std::unique_ptr<EVP_PKEY_CTX, OpenSslDeleter<EVP_PKEY_CTX, EVP_PKEY_CTX_free>>;
using X509Ptr = std::unique_ptr<X509, OpenSslDeleter<X509, X509_free>>;
using X509ExtensionPtr = std::unique_ptr<X509_EXTENSION, OpenSslDeleter<X509_EXTENSION, X509_EXTENSION_free>>;

std::string last_openssl_error() {
  const unsigned long error_code = ERR_get_error();
  if (error_code == 0) {
    return "unknown OpenSSL error";
  }

  std::array<char, 256> error_buffer{};
  ERR_error_string_n(error_code, error_buffer.data(), error_buffer.size());
  return std::string(error_buffer.data());
}

bool add_name_entry(X509_NAME* name, const char* field, const char* value) {
  return X509_NAME_add_entry_by_txt(
             name, field, MBSTRING_ASC, reinterpret_cast<const unsigned char*>(value), -1, -1, 0) == 1;
}

bool add_extension(X509* certificate, const int extension_id, const char* extension_value) {
  X509V3_CTX context;
  X509V3_set_ctx_nodb(&context);
  X509V3_set_ctx(&context, certificate, certificate, nullptr, nullptr, 0);
  X509ExtensionPtr extension(X509V3_EXT_nconf_nid(nullptr, &context, extension_id, extension_value));
  return extension != nullptr && X509_add_ext(certificate, extension.get(), -1) == 1;
}

std::string bio_to_string(BIO* bio) {
  char* buffer = nullptr;
  const long length = BIO_get_mem_data(bio, &buffer);
  if (length <= 0 || buffer == nullptr) {
    return {};
  }
  return std::string(buffer, static_cast<std::size_t>(length));
}

EvtPkeyPtr generate_private_key(std::string* error_message) {
  EvtPkeyCtxPtr key_generation_context(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr));
  if (key_generation_context == nullptr) {
    *error_message = "Failed to allocate TLS test key generation context: " + last_openssl_error();
    return nullptr;
  }

  if (EVP_PKEY_keygen_init(key_generation_context.get()) != 1 ||
      EVP_PKEY_CTX_set_rsa_keygen_bits(key_generation_context.get(), kRsaKeyBits) != 1) {
    *error_message = "Failed to initialize TLS test key generation: " + last_openssl_error();
    return nullptr;
  }

  EVP_PKEY* generated_private_key = nullptr;
  if (EVP_PKEY_keygen(key_generation_context.get(), &generated_private_key) != 1 || generated_private_key == nullptr) {
    *error_message = "Failed to generate TLS test private key: " + last_openssl_error();
    return nullptr;
  }

  return EvtPkeyPtr(generated_private_key);
}

TestTlsMaterial generate_localhost_tls_material() {
  ERR_clear_error();
  TestTlsMaterial material;

  EvtPkeyPtr private_key = generate_private_key(&material.error_message);
  X509Ptr certificate(X509_new());
  if (private_key == nullptr || certificate == nullptr) {
    if (certificate == nullptr && material.error_message.empty()) {
      material.error_message = "Failed to allocate TLS test material: " + last_openssl_error();
    }
    return material;
  }

  if (X509_set_version(certificate.get(), kX509Version) != 1 ||
      ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), kCertificateSerialNumber) != 1 ||
      X509_gmtime_adj(X509_get_notBefore(certificate.get()), 0) == nullptr ||
      X509_gmtime_adj(X509_get_notAfter(certificate.get()), kCertificateValiditySeconds) == nullptr ||
      X509_set_pubkey(certificate.get(), private_key.get()) != 1) {
    material.error_message = "Failed to initialize TLS test certificate: " + last_openssl_error();
    return material;
  }

  X509_NAME* subject_name = X509_get_subject_name(certificate.get());
  if (subject_name == nullptr || !add_name_entry(subject_name, "CN", kCommonName) ||
      !add_name_entry(subject_name, "O", kOrganizationName) ||
      X509_set_issuer_name(certificate.get(), subject_name) != 1 ||
      !add_extension(certificate.get(), NID_basic_constraints, kBasicConstraints) ||
      !add_extension(certificate.get(), NID_key_usage, kKeyUsage) ||
      !add_extension(certificate.get(), NID_ext_key_usage, kExtendedKeyUsage) ||
      !add_extension(certificate.get(), NID_subject_alt_name, kSubjectAltName) ||
      X509_sign(certificate.get(), private_key.get(), EVP_sha256()) <= 0) {
    material.error_message = "Failed to finalize TLS test certificate: " + last_openssl_error();
    return material;
  }

  BioPtr certificate_bio(BIO_new(BIO_s_mem()));
  BioPtr private_key_bio(BIO_new(BIO_s_mem()));
  if (certificate_bio == nullptr || private_key_bio == nullptr ||
      PEM_write_bio_X509(certificate_bio.get(), certificate.get()) != 1 ||
      PEM_write_bio_PrivateKey(private_key_bio.get(), private_key.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1) {
    material.error_message = "Failed to serialize TLS test material: " + last_openssl_error();
    return material;
  }

  material.certificate_chain_pem = bio_to_string(certificate_bio.get());
  material.private_key_pem = bio_to_string(private_key_bio.get());
  material.ok = !material.certificate_chain_pem.empty() && !material.private_key_pem.empty();
  if (!material.ok) {
    material.error_message = "Failed to serialize TLS test material into PEM strings.";
  }
  return material;
}

}  // namespace

const TestTlsMaterial& localhost_tls_material() noexcept {
  static const TestTlsMaterial kMaterial = generate_localhost_tls_material();
  return kMaterial;
}

}  // namespace task_orchestrator::protocol::test_support
