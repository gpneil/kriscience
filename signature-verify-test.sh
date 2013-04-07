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

tmpdir=$(mktemp -d)

pass=none
key_priv="${tmpdir}/test-key-priv.pem"
cert="${tmpdir}/test-cert.pem"
data="${tmpdir}/data"
signature="${tmpdir}/signature"

# generate private key and certificate with public key 
openssl req -x509 -newkey rsa:2048 \
    -keyout "${key_priv}" -subj "/CN=FakeSigner" \
    -passout pass:"${pass}" -out "${cert}" &>/dev/null

# generate data and signature
echo -n "test" | \
tee "${data}" | \
openssl dgst -sha1 -sign "${key_priv}" -passin pass:"${pass}" \
    -out "${signature}"

let failed=0
test_fail() {
    local test=$1
    echo "Test $test failed"
    ((++failed))
}

# test successful verification
"${executable}" "${cert}" "${data}" "${signature}" | grep -q "OK" || \
    test_fail "successful-verification"

# test tampered data verification
data_tampered="${data}.tampered"
cp -a "${data}" "${data_tampered}"
echo "whoa" >> "${data_tampered}"
"${executable}" "${cert}" "${data_tampered}" "${signature}" |& grep -q "failed" || \
    test_fail "tampered-verification"

rm -rf "${tmpdir}"

(($failed == 0)) && echo "All tests passed"
exit $failed
