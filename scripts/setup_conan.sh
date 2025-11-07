#!/usr/bin/env bash
set -euo pipefail

RELEASE_ONLY=false
DEBUG_ONLY=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    --release-only)
      RELEASE_ONLY=true
      shift
      ;;
    --debug-only)
      DEBUG_ONLY=true
      shift
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 1
      ;;
  esac
done

PROFILE_PATH="${HOME}/.conan2/profiles/default"
if [[ ! -f "${PROFILE_PATH}" ]]; then
  echo "[setup_conan] Default profile missing. Running 'conan profile detect'."
  conan profile detect >/dev/null
fi

invoke_conan_install() {
  local build_type="$1"
  local output_folder="$(pwd)"
  echo "[setup_conan] Running 'conan install . --build=missing -s build_type=${build_type}."
  conan install . --build=missing -s "build_type=${build_type}" -of "${output_folder}"
}

if [[ "${RELEASE_ONLY}" != true ]]; then
  invoke_conan_install "Debug"
fi

if [[ "${DEBUG_ONLY}" != true ]]; then
  invoke_conan_install "Release"
fi
