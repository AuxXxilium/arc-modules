#!/usr/bin/env bash
#
# Add the sha256 of every downloaded input that checksums.sha256 does not pin
# yet: everything in the cache (gpl/, toolkit/, toolchain/, oot/) and the
# toolkit files the syno-compiler folder provides for the targets.
#
# scripts/pin-checksums.sh [cache-dir] [syno-compiler-dir]
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CACHE="${1:-${ROOT}/cache}"
TK="${2:-/root/syno-compiler}"
SUMS="${ROOT}/checksums.sha256"
touch "${SUMS}"

pin() { # pin <file> <key>
  if grep -q "  $2\$" "${SUMS}"; then return 0; fi
  echo "$(sha256sum "$1" | awk '{print $1}')  $2" | tee -a "${SUMS}"
}

if [ -d "${CACHE}" ]; then
  (cd "${CACHE}" && find gpl toolkit toolchain oot -type f \( -name '*.txz' -o -name '*.tar.*' -o -name '*.tgz' \) 2>/dev/null || true) |
    while read -r f; do pin "${CACHE}/${f}" "${f}"; done
fi

while IFS=';' read -r p d k g b n; do
  [[ -z "${p}" || "${p}" == \#* ]] && continue
  f="${TK}/cache-${d}/ds.${p}-${d}.dev.txz"
  if [ -s "${f}" ]; then pin "${f}" "toolkit/${d}/ds.${p}-${d}.dev.txz"; fi
  f="${TK}/cache-${d}/${p}-gcc1220_glibc236_x86_64-GPL.txz"
  if [ -s "${f}" ]; then pin "${f}" "toolchain/${b}/${p}-gcc1220_glibc236_x86_64-GPL.txz"; fi
done <"${ROOT}/platforms.conf"

sort -k2 -u "${SUMS}" -o "${SUMS}"
