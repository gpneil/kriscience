#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

/*
 * A handy alias for return types from functions.  Don't bother
 * specifying deleter type for unique_ptr.
 */
template<class T>
using scoped_ptr = std::unique_ptr<T, std::function<void (T *const)>>;

/*
 * Avoid writing any types explicitly.  Using it with `auto' makes
 * it really straight forward.
 */
template<class T, class D = std::default_delete<T>>
constexpr std::unique_ptr<T, D>
make_scoped (T *const ptr, D deleter = D ())
{
  return std::unique_ptr<T, D> (ptr, deleter);
}

/*
 * Read PEM certificate from a file and return as a new X509 structure.
 */
X509 *
read_x509_raw (const std::string& path)
{
  const auto bio = make_scoped (BIO_new_file (path.c_str (), "r"), BIO_free);
  if (!bio) {
    throw std::runtime_error ("Cannot open file: " + path);
  }

  X509 *const cert = PEM_read_bio_X509 (bio.get (), nullptr, nullptr, nullptr);
  if (!cert) {
    throw std::runtime_error ("Cannot read certificate: " + path);
  }

  return cert;
}

/*
 * A RAII wrapper around read_x509_raw().
 */
scoped_ptr<X509>
read_x509 (const std::string& path)
{
  return make_scoped (read_x509_raw (path), X509_free);
}

/*
 * Read a range of untrusted certificate files and return a X509 stack
 * of certificates.
 */
scoped_ptr<STACK_OF(X509)>
read_untrusted (char const *const *begin, char const *const *end)
{
  scoped_ptr<STACK_OF(X509)> untrusted_certs =
    make_scoped (
      sk_X509_new_null (),
      [](STACK_OF(X509)* const s) { sk_X509_pop_free (s, X509_free); });

  std::for_each (begin, end, [&untrusted_certs] (char const *path) {
      if (!sk_X509_push (untrusted_certs.get (), read_x509_raw (path))) {
	throw
	  std::runtime_error ("Cannot add untrusted certificate into the stack");
      }
    }
  );

  return untrusted_certs;
}

/*
 * Print a message for a failed verification.
 */
void
print_verification_failure_msg (X509_STORE_CTX *const ctx, std::ostream& out)
{
  // This is the main error code
  const int error = X509_STORE_CTX_get_error (ctx);

  // ... and a corresponding error message.
  char const* const error_msg = X509_verify_cert_error_string (error);
  if (!error_msg) {
    throw std::runtime_error ("Cannot get verification failure message");
  }

  // Now to get more information about the verification failure we need to call
  // a number of functions that can only write to BIO.  So let's allocate one
  // in memory.
  const auto bio = make_scoped (BIO_new (BIO_s_mem ()), BIO_vfree);
  if (!bio) {
    throw std::runtime_error ("Cannot allocate buffer for the error message");
  }

  // Maybe we can find out something about the culprit certificate...
  X509* const cert = X509_STORE_CTX_get_current_cert (ctx);
  // It is possible that the cert is deliberately set to NULL and the error
  // is not related to any specific certificate.
  if (cert) {
    X509_NAME* const subj = X509_get_subject_name (cert);
    if (!subj) {
      throw std::runtime_error ("Cannot get certificate subject name");
    }

    if (X509_NAME_print_ex (bio.get (), subj, 0, XN_FLAG_ONELINE) <= 0) {
      throw std::runtime_error ("Cannot get certificate subject name");
    }
  }

  switch (error)
  { 
  case X509_V_ERR_CERT_NOT_YET_VALID:
  case X509_V_ERR_CERT_HAS_EXPIRED:
    // If it's somthing to do with certificate validity dates (one of the most
    // common failure reasons), lets try to find out something more about them.
    if (BIO_puts (bio.get (),"\nnotBefore: ") <= 0 ||
	ASN1_TIME_print (bio.get (), X509_get_notBefore (cert)) <= 0 ||
	BIO_puts (bio.get (),"\nnotAfter: ") <= 0 ||
	ASN1_TIME_print (bio.get (), X509_get_notAfter (cert)) <= 0) {
      throw std::runtime_error ("Cannot get certificate expiration dates");
    }

    break;
  }

  // Now let's extract all we've collected in our BIO object
  char* buffer;
  const long buffer_length = BIO_get_mem_data (bio.get (), &buffer);
  if (buffer_length < 0) {
    throw std::runtime_error ("Cannot get error message buffer");
  }

  // And it's also worth to know at what certificate level the verification failed.
  out << error_msg << " (at level " << X509_STORE_CTX_get_error_depth (ctx) << ')';
  if (buffer_length > 0) {
    out << '\n';
    out.write (buffer, buffer_length);
  }

  out << std::endl;
}

void
usage (char const *const arg0, std::ostream& out)
{
  out <<
"Usage:\n"
"\n"
"    " << arg0 << " <CA cert> [<untrusted cert>...] <leaf cert>\n"
"\n"
"Verifies <leaf cert> with all optional intermediate <untrusted cert>s and\n"
"ultimately trusted root <CA cert>.  Please note that the order of certificates\n"
"on the command line is important.\n"
      << std::endl;
}

bool
help_requested (const std::string& arg)
{
  return arg == "-h" || arg == "-help" || arg == "--help";
}

int
main (int argc, char* argv[])
{
  // help
  if (argc == 2 && help_requested (argv[1])) {
    usage (argv[0], std::cout);
    return EXIT_SUCCESS;
  }

  // misuse
  if (argc < 3) {
    usage (argv[0], std::cerr);
    return EXIT_FAILURE;
  }

  // This has to be called before the verification where a number of cryptographic
  // algorithms is used (they are not available by default).
  OpenSSL_add_all_algorithms ();

  try {
    // Let's create a certificate store for the root CA
    const auto trusted = make_scoped (X509_STORE_new (), X509_STORE_free);
    if (!trusted) {
      throw std::runtime_error ("Cannot create a trusted store");
    }

    // The lookup method is owned by the store
    const auto lookup = X509_STORE_add_lookup (trusted.get (), X509_LOOKUP_file ());
    if (!lookup) {
      throw std::runtime_error ("Cannot add file lookup");
    }

    // Load the root CA into the store
    if (1 != X509_LOOKUP_load_file (lookup, argv[1], X509_FILETYPE_PEM)) {
      throw std::runtime_error ("Cannot load root CA");
    }

    // Create a X509 store context required for the verification
    const auto ctx = make_scoped (X509_STORE_CTX_new (), X509_STORE_CTX_free);
    if (!ctx) {
      throw std::runtime_error ("Cannot create a store context");
    }

    // Now our untrusted (intermediate) certificates (if any)
    const auto untrusted = read_untrusted (argv + 2, argv + argc - 1);
    // And our leaf certificate we want to verify
    const auto cert = read_x509 (argv[argc - 1]);

    // Initialize the context for the verifiacion
    if (!X509_STORE_CTX_init (
	  ctx.get (), trusted.get (), cert.get (), untrusted.get ())) {
      throw std::runtime_error ("Cannot initialize the store context");
    }

    // Verify!
    const int result = X509_verify_cert (ctx.get ());
    if (result < 0) {
      throw std::runtime_error ("Cannot verify certificates");
    }

    std::cout << "Verification " << (result ? "OK" : "failed") << std::endl;
    if (!result) {
      ERR_load_X509_strings (); // required before decoding error code into a string

      // print something useful about the failure
      print_verification_failure_msg (ctx.get (), std::cerr);
    }
  }
  catch (const std::exception& e) {
    std::cerr << "Error: " << e.what () << '\n';
    return EXIT_FAILURE;
  }
  catch (...) {
    std::cerr << "Unknown error\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
