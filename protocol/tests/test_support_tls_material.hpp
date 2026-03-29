#ifndef TASK_ORCHESTRATOR__PROTOCOL_TESTS__TEST_SUPPORT_TLS_MATERIAL_HPP_
#define TASK_ORCHESTRATOR__PROTOCOL_TESTS__TEST_SUPPORT_TLS_MATERIAL_HPP_

#include <string>

namespace task_orchestrator::protocol::test_support {

/**
 * @brief Runtime-generated localhost TLS identity used by transport tests.
 */
struct TestTlsMaterial {
  bool ok = false;
  std::string error_message;
  std::string certificate_chain_pem;
  std::string private_key_pem;
};

/**
 * @brief Return a cached self-signed certificate and private key for localhost transport tests.
 */
const TestTlsMaterial& localhost_tls_material() noexcept;

}  // namespace task_orchestrator::protocol::test_support

#endif  // TASK_ORCHESTRATOR__PROTOCOL_TESTS__TEST_SUPPORT_TLS_MATERIAL_HPP_
