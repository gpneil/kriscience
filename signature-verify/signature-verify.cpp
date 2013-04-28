#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

/**
 * We want to abstract away from the exact byte type in the code.
 */
typedef unsigned char byte_t;

/*
 * A handy alias for return types from functions.  Don't bother
 * specifying deleter type for std::unique_ptr any more.
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
scoped_ptr<X509>
read_x509 (const std::string& path)
{
  const auto bio = make_scoped (BIO_new_file (path.c_str (), "r"), BIO_free);
  if (!bio) {
    throw std::runtime_error ("Cannot open file: " + path);
  }

  X509 *const cert = PEM_read_bio_X509 (bio.get (), nullptr, nullptr, nullptr);
  if (!cert) {
    throw std::runtime_error ("Cannot read certificate: " + path);
  }

  return make_scoped (cert, X509_free);
}

std::vector<byte_t>
read_file (const std::string& path)
{
  std::ifstream in (path, std::ios::binary);
  if (!in) {
    throw std::runtime_error ("Cannot open file: " + path);
  }

  return std::vector<byte_t> (
    // ok, we don't want to run into issues with locale
    // so using char rather than byte_t and deferring conversion to
    // vector construction
    std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char> ());
}

void
usage (char const *const arg0, std::ostream& out)
{
  out <<
"Usage:\n"
"\n"
"    " << arg0 << " <PEM cert> <data file> <signature file>\n"
"\n"
"Verifies <data file> signature stored in <signature file> with certificate\n"
"in <PEM cert>.\n"
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
  if (argc != 4) {
    usage (argv[0], std::cerr);
    return EXIT_FAILURE;
  }

  // This has to be called before the verification where a number of cryptographic
  // algorithms is used (they might not be available by default).
  OpenSSL_add_all_algorithms ();

  try {
    // read the certificate
    const auto cert = read_x509 (argv[1]);
    // get the public key from the certificate (the signature was created with
    // the private key)
    const auto key = make_scoped (X509_get_pubkey (cert.get ()), EVP_PKEY_free);
    if (!key) {
      throw std::runtime_error ("Cannot get public key");
    }

    // create an enveloped message digest context
    const auto ctx = make_scoped (EVP_MD_CTX_create (), EVP_MD_CTX_destroy);

    // initialize the MD context with SHA1 algorithm (the choice of the
    // algorithm should be more flexible in most real cases)
    if (!EVP_VerifyInit_ex (ctx.get (), EVP_sha1 (), nullptr)) {
      throw std::runtime_error("Cannot initialize verification");
    }

    // the data about to be digested
    const std::vector<byte_t> data = read_file (argv[2]);

    // the signature signed by the private key paired with the public key we're
    // about to use (the public key is delivered with the certificate so it's
    // authenticity can be verified)
    const std::vector<byte_t> signature = read_file (argv[3]);

    // calculate the digest
    if (!EVP_VerifyUpdate (ctx.get (), data.data (), data.size ())) {
      throw std::runtime_error("Failed to process data");
    }

    // verify the digest
    const int result =
      EVP_VerifyFinal (
	ctx.get (), signature.data (), signature.size (), key.get ());

    if (result < 0) {
      throw std::runtime_error ("Verification error");
    }

    if (result != 0) {
      std::cout << "Verification OK" << std::endl;
      return EXIT_SUCCESS;
    }
    else {
      std::cerr << "Verification failed\n";
      return EXIT_FAILURE;
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
}
