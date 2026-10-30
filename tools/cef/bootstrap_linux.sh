#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=pin.env
source "${SCRIPT_DIR}/pin.env"

CACHE_DIR="${1:-${PWD}/.cache/cef}"
EXTRACT_DIR="${2:-${PWD}/.cache/cef/extracted}"

mkdir -p "${CACHE_DIR}" "${EXTRACT_DIR}"

archive_path="${CACHE_DIR}/${CEF_ARCHIVE}"
url="${CEF_BASE_URL}/${CEF_ARCHIVE}"
root_name="${CEF_ARCHIVE%.tar.bz2}"
cef_root="${EXTRACT_DIR}/${root_name}"

if [[ ! -f "${archive_path}" ]]; then
  echo "Downloading ${url}"
  curl     --fail     --location     --show-error     --retry 4     --retry-delay 3     "${url}"     -o "${archive_path}.partial"
  mv "${archive_path}.partial" "${archive_path}"
else
  echo "Using cached archive: ${archive_path}"
fi

actual_sha1="$(sha1sum "${archive_path}" | awk '{print $1}')"

if [[ "${actual_sha1}" != "${CEF_SHA1}" ]]; then
  echo "CEF SHA1 mismatch" >&2
  echo "expected: ${CEF_SHA1}" >&2
  echo "actual  : ${actual_sha1}" >&2
  rm -f "${archive_path}"
  exit 1
fi

echo "CEF archive SHA1: PASS"

if [[ ! -f "${cef_root}/CMakeLists.txt" ]]; then
  echo "Extracting CEF to ${EXTRACT_DIR}"
  rm -rf "${cef_root}"
  tar -xjf "${archive_path}" -C "${EXTRACT_DIR}"
else
  echo "Using extracted CEF: ${cef_root}"
fi

required=(
  "CMakeLists.txt"
  "README.txt"
  "include/cef_app.h"
  "Release/libcef.so"
  "Resources/icudtl.dat"
)

for relative in "${required[@]}"; do
  if [[ ! -e "${cef_root}/${relative}" ]]; then
    echo "Missing required CEF distribution file: ${relative}" >&2
    exit 1
  fi
done

printf '%s\n' "${cef_root}"
