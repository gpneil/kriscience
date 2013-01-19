#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>

template<class T, class D = std::default_delete<T>>
constexpr std::unique_ptr<T, D>
make_scoped(T *const ptr, D deleter = D())
{
  return std::unique_ptr<T, D>(ptr, deleter);
}

void
addPolicy(X509_VERIFY_PARAM *const params, const std::string& policy)
{
  auto policyObj =
    make_scoped(OBJ_txt2obj(policy.c_str(), 1), ASN1_OBJECT_free);
    
  if (!policyObj) {
    throw std::runtime_error("Cannot create policy object");
  }

  if (!X509_VERIFY_PARAM_add0_policy(params, policyObj.get())) {
    throw std::runtime_error("Cannot add policy");
  }

  // committed
  (void)policyObj.release();
}

void
someRsaStuff()
{
  auto rsa = make_scoped(RSA_new(), RSA_free);
  // do some stuff with the RSA key
  // ...
}

int
main()
{
  const auto params =
    make_scoped(X509_VERIFY_PARAM_new(), X509_VERIFY_PARAM_free);
    
  (void)X509_VERIFY_PARAM_set_flags(
    params.get(), X509_V_FLAG_POLICY_CHECK | X509_V_FLAG_EXPLICIT_POLICY);
  (void)X509_VERIFY_PARAM_clear_flags(
    params.get(), X509_V_FLAG_INHIBIT_ANY | X509_V_FLAG_INHIBIT_MAP);
    
  addPolicy(params.get(), "1.2.3.4");
  
  // set up other stuff and do the verification
  // ...
  
  const auto untrustedCerts =
    make_scoped(
      sk_X509_new_null(),
      [](STACK_OF(X509)* const s) { sk_X509_pop_free(s, X509_free); });
  // now add untrusted certificated to the stack
  // ...
  
  return EXIT_SUCCESS;
}

