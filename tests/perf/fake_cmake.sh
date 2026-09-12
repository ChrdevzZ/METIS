#!/bin/bash
set -u
[ -z "${FAKE_CMAKE_MARKER:-}" ] || printf 'called\n' >>"$FAKE_CMAKE_MARKER"
exit 0
