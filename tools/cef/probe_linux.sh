#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=pin.env
source "${SCRIPT_DIR}/pin.env"

url="${CEF_BASE_URL}/${CEF_ARCHIVE}"
sha1_url="${url}.sha1"

echo "CEF version : ${CEF_VERSION}"
echo "CEF archive : ${CEF_ARCHIVE}"
echo "CEF URL     : ${url}"

tmp_sha1="$(mktemp)"
trap 'rm -f "${tmp_sha1}"' EXIT

curl   --fail   --location   --silent   --show-error   --retry 3   --retry-delay 2   "${sha1_url}"   -o "${tmp_sha1}"

expected_sha1="$(tr -d '[:space:]' < "${tmp_sha1}")"

if [[ ! "${expected_sha1}" =~ ^[0-9a-fA-F]{40}$ ]]; then
  echo "Invalid SHA1 response from ${sha1_url}: ${expected_sha1}" >&2
  exit 1
fi

echo "CEF SHA1    : ${expected_sha1}"

if [[ "${expected_sha1}" != "${CEF_SHA1}" ]]; then
  echo "Pinned CEF SHA1 does not match remote artifact" >&2
  echo "pinned: ${CEF_SHA1}" >&2
  echo "remote: ${expected_sha1}" >&2
  exit 1
fi

echo "CEF artifact probe: PASS"
