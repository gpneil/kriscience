#!/bin/bash -e

#/ Usage: signature-verify-test.sh <signature-verify executable>
#/
#/ Runs basic tests of a program implemented in signature-verify.cpp and built as
#/ <signature-verify executable>.
#/
#/ Examples:
#/    signature-verify-test.sh /tmp/signature-verify

usage() { grep '^#/' "$0" | cut -c 4-; }

executable="$1"
[ -z "$executable" ] && {
	echo "No executable specified" >&2
	echo
	usage
	exit 1
}

# scratch directory
tmpdir=$(mktemp -d)

# Don't care about the password as this is all fake material for testing
pass="none"

# private key
key_priv="${tmpdir}/test-key-priv.pem"

# associated certificate
cert="${tmpdir}/test-cert.pem"

# data to sign (and verify)
data="${tmpdir}/data"

# generated signature which is delivered for verification to the peer
signature="${tmpdir}/signature"

##
# generate private key and certificate with public key
#
openssl req -x509 -newkey rsa:2048 \
    -keyout "${key_priv}" -subj "/CN=FakeSigner" \
    -passout pass:"${pass}" -out "${cert}" &>/dev/null

##
# generate data and sign it (generate the signature)
#
echo -n "test" | \
tee "${data}" | \
openssl dgst -sha1 -sign "${key_priv}" -passin pass:"${pass}" \
    -out "${signature}"

###############
#### tests ####
###############

##
# test successful verification
#
echo -n "Expecting successful verification ... "
"${executable}" "${cert}" "${data}" "${signature}"

##
# test tampered data verification
#
data_tampered="${data}.tampered"
cp -a "${data}" "${data_tampered}"
echo "whoa" >> "${data_tampered}"
echo -n "Expecting verification failure ... "
"${executable}" "${cert}" "${data_tampered}" "${signature}"

##
# remove the scratch directory
#
rm -rf "${tmpdir}"
