#!/usr/bin/env bash
cd "$(dirname "$0")"
WHEELCC="${WHEELCC:-./bin/driver.sh}"
fail=0
check() { "$WHEELCC" -o /tmp/pt "$1" >/dev/null 2>&1 && /tmp/pt; got=$?; if [ "$got" = "$2" ]; then echo "ok   $1"; else echo "FAIL $1 (want $2, got $got)"; fail=1; fi; }
check demo/hello.pr 42
check demo/kernel.pr 7
check demo/i16.pr 44
check demo/neg.pr 254
exit $fail
check demo/pack.pr 5
