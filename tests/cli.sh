#!/bin/sh
set -eu
export LC_ALL=C

cli=$1
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
trap 'exit 1' HUP INT TERM

out="$tmp/stdout"
err="$tmp/stderr"
valid="$tmp/valid input.bminor"
malformed="$tmp/malformed input.bminor"

fail() {
    printf 'CLI test failed: %s\n' "$*" >&2
    cat "$err" >&2
    exit 1
}

check_status() {
    name=$1
    expected=$2
    shift 2
    status=0
    timeout 5s "$@" >"$out" 2>"$err" || status=$?
    [ "$status" -eq "$expected" ] || fail "$name expected status $expected, got $status"
}

cat >"$valid" <<'BMINOR'
heartbeat: function integer ();
m: function void () = {
    print heartbeat();
}
BMINOR

cat >"$malformed" <<'BMINOR'
m: function void () = {
    print ;
    @
    print heartbeat();
}
BMINOR

check_status "valid HTML" 0 "$cli" --format html "$valid"
[ ! -s "$err" ] && grep -Fq '<span class="fn">heartbeat</span>()' "$out" ||
    fail "valid HTML must render the highlighted call without diagnostics"

call=$(printf '\033[93mheartbeat\033[0m()')
check_status "valid ANSI" 0 "$cli" --format ansi "$valid"
[ ! -s "$err" ] && grep -Fq "$call" "$out" ||
    fail "valid ANSI must render the highlighted call without diagnostics"

for format in ansi html; do
    check_status "malformed $format" 1 "$cli" --format "$format" "$malformed"
    [ -s "$out" ] || fail "malformed $format must render source"
    [ "$(wc -l <"$err")" -eq 2 ] || fail "both source errors must be on stderr"
    sed -n '1p' "$err" | grep -Fq "$malformed:2:11: error [PARSE_EXPECTED]:" ||
        fail "syntax diagnostic must come first in source order"
    sed -n '2p' "$err" | grep -Fq "$malformed:3:5: error [LEX_UNKNOWN_BYTE]:" ||
        fail "lexical diagnostic must carry filename and location"
    if [ "$format" = html ]; then
        grep -Fq '<span class="fn">heartbeat</span>()' "$out" ||
            fail "malformed HTML must highlight the recovered call"
        grep -Fq '<span class="err">;</span>' "$out" ||
            fail "malformed HTML must style the erroneous delimiter"
    else
        grep -Fq "$call" "$out" || fail "malformed ANSI must highlight the recovered call"
    fi
done

check_status "empty CLI" 2 "$cli"
[ ! -s "$out" ] && [ -s "$err" ] || fail "missing arguments must report only on stderr"

check_status "bad format" 2 "$cli" --format json "$valid"
[ ! -s "$out" ] && [ -s "$err" ] || fail "invalid format must report only on stderr"

check_status "missing input" 2 "$cli" --format html "$tmp/no-such-file"
[ ! -s "$out" ] && [ -s "$err" ] || fail "missing file must report only on stderr"

check_status "directory input" 2 "$cli" --format html "$tmp"
[ ! -s "$out" ] && [ -s "$err" ] || fail "directory input must report only on stderr"

check_status "help" 0 "$cli" --help
[ ! -s "$err" ] && grep -Fqx 'Usage: bminor-highlight --format ansi|html INPUT' "$out" ||
    fail "help must print usage on stdout"

if [ -c /dev/full ]; then
    status=0
    timeout 5s "$cli" --format html "$valid" >/dev/full 2>"$err" || status=$?
    [ "$status" -eq 2 ] && grep -Fq 'Could not write to stdout' "$err" ||
        fail "buffered output errors must be checked on flush"
fi
