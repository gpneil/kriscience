#!/bin/bash

#/ Usage: test-c14n.sh <c14n-executable> <sample XML>
#/
#/ Runs a simple XMLDSig signature verification of <sample XML> file by using
#/ <c14n-executable> to canonicalise elements of the XML files as needed.  It's
#/ supposed to demonstrate using libxml2 C14N functionality programatically on
#/ a real example.  Please note that there's no certificate verification prior
#/ to signature verification as it's out of scope of this example.
#/
#/ Examples:
#/   ./test-c14n.sh /tmp/c14n-exe /tmp/sample.xml

function usage() { grep '^#/' "$0" | cut -c 4-; }

function failure_missing_argument() {
  local arg_desc="$1"

  echo "Missing argument:  $arg_desc" >&2
  echo
  usage
  exit 1
}

xmldsig_c14n_exe="$1"
sample_xml="$2"

[ -z "$xmldsig_c14n_exe" ] && \
  failure_missing_argument "an executable to get C14N XML sub-document"

[ -z "$sample_xml" ] && \
    failure_missing_argument "sample XML file"

function get_xml_node_text() {
  local node=$1
  local let which=$2
  local from=$3

  xmllint \
    --xpath "//*[local-name()=\"$node\"][$which]/text()" \
    $from
}

function get_cert() {
  local let which=$1
  local from=$2

  printf -- \
    "-----BEGIN CERTIFICATE-----\n%s\n-----END CERTIFICATE-----\n" \
    "$(get_xml_node_text X509Certificate $which $from)"
}

function get_pub_key() {
  local let which=$1
  local from=$2

  get_cert $which $from | openssl x509 -pubkey -noout
}

function get_signature() {
  local let which=$1
  local from=$2

  get_xml_node_text SignatureValue $which $from | \
  openssl base64 -d
}

# This bit is digested and the digest is signed.  It contains a digest of some
# other part of the XML.
si_xpath="/default:Signature/default:SignedInfo"

$xmldsig_c14n_exe $si_xpath $sample_xml |
openssl dgst -sha1 -binary \
  -verify <(get_pub_key 1 $sample_xml) \
  -signature <(get_signature 1 $sample_xml)
