#define LIBXML_C14N_ENABLED
#define LIBXML_OUTPUT_ENABLED

#include <libxml/c14n.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include <cstdlib>
#include <iostream>
#include <memory>

/**
 * A common utility for creating RAII objects.
 */
template<class T, class D = std::default_delete<T>>
constexpr std::unique_ptr<T, D>
make_scoped (T *const ptr, D deleter = D ())
{
  return std::unique_ptr<T, D> (ptr, deleter);
}

/**
 * Any libxml2 global initialisation/clean-up happens here in a RAII fashion.
 */
struct XmlContext {
  XmlContext ()
  {
    xmlInitParser ();
    LIBXML_TEST_VERSION
  }

  ~XmlContext ()
  {
    xmlCleanupParser ();
  }
};

void
usage (char const *const arg0, std::ostream& out)
{
  out <<
"Usage:\n"
"\n"
"    " << arg0 << " <XPath expr> [<XML input file> [<XML output file>]]\n"
"\n"
"Canonicalises (C14N, see [1]) <XML infput file> which is assumed to be\n"
"an XML Signature (XMLDSig) file [2].  This briefly means removing redundant\n"
"white space characters, reordering XML nodes etc. (see [1] for more details)\n"
"so that it has standard formatting applied ready for signing or verifying.\n"
"The first (top-level) node to process is specified with <XPath expr>.\n"
"\n"
"The result is written to <XML output file>.\n"
"\n"
"If <XML output file> is not provided, the result is printed into\n"
"the standard output.  If <XML input file> is also not provided, it's read\n"
"from the standard input.\n"
"\n"
"[1] http://www.w3.org/TR/xml-c14n\n"
"[2] http://www.w3.org/TR/xmldsig-core\n"
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
  if (argc < 2 || 4 < argc) {
    usage (argv[0], std::cerr);
    return EXIT_FAILURE;
  }

  char const *const file_out = argc > 3 ? argv[3] : "/dev/stdout";
  char const *const file_in = argc > 2 ? argv[2] : "/dev/stdin";

  XmlContext context;

  // First, let's parse the XML file.
  const auto doc = make_scoped (xmlParseFile (file_in), xmlFreeDoc);
  if (!doc) {
    std::cerr << "Unable to parse file '" << file_in << "'\n";
    return EXIT_FAILURE;
  }

  // Now we need a context for our bonkers XPath expressions
  const auto ctx = make_scoped (xmlXPathNewContext (doc.get ()),
				xmlXPathFreeContext);
  if (!ctx) {
    std::cerr << "Unable to create new XPath context\n";
    return EXIT_FAILURE;
  }

  // It's handy to have a well known alias to an XML namespace which we can use
  // later on in XPath expressions.
  const xmlChar DEFAULT_NS_PREFIX[] = "default";
  const xmlChar XMLDSIG_NAMESPACE[] = "http://www.w3.org/2000/09/xmldsig#";
  if (xmlXPathRegisterNs (ctx.get (), DEFAULT_NS_PREFIX, XMLDSIG_NAMESPACE) != 0) {
    std::cerr
      << "Unable to register namespace " << DEFAULT_NS_PREFIX
      << "=" << XMLDSIG_NAMESPACE << '\n';
    return EXIT_FAILURE;
  }

  // This is what we'll be extracting.
  xmlChar *const siPath = reinterpret_cast<xmlChar*>(argv[1]);

  // First, lets "focus" on our signature information bit.
  const auto sinfo_node =
    make_scoped (xmlXPathEvalExpression (siPath, ctx.get ()), xmlXPathFreeObject);
  if (!sinfo_node || xmlXPathNodeSetIsEmpty (sinfo_node->nodesetval)) {
    std::cerr << "Unable to get object: " << siPath << '\n';
    return EXIT_FAILURE;
  }

  // From now on we're operating within the signature information sub-document.
  ctx->node = xmlXPathNodeSetItem (sinfo_node->nodesetval, 0);

  // Now this is old granny's secret recipe for a delicious cheesecake.
  const xmlChar C14N_CONTENT[] =
    "descendant-or-self::* | descendant-or-self::text()[normalize-space(.)]"
    "| .//attribute::* | .//namespace::* | .//comment()";

  // Get some cheese... all sub-document content we need for canonicalisation.
  const auto sinfo =
    make_scoped (xmlXPathEvalExpression (C14N_CONTENT, ctx.get ()),
		 xmlXPathFreeObject);
  if (!sinfo || !sinfo->nodesetval || sinfo->nodesetval->nodeNr == 0) {
    std::cerr << "Unable to get object(s) from path: " << C14N_CONTENT << '\n';
    return EXIT_FAILURE;
  }

  // And finally... bake it!
  if (xmlC14NDocSave (doc.get (), sinfo->nodesetval, 0, NULL, 0, file_out, 0) < 0) {
    std::cerr << "ERROR: Cannot save selected doc/nodes into canonicalised form\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
