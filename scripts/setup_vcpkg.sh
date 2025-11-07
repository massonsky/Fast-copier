#!/usr/bin/env bash
set -euo pipefail

TRIPLETS=()
CLEAN_OUTDATED=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    --triplet)
      shift
      [[ $# -gt 0 ]] || { echo "--triplet requires an argument" >&2; exit 1; }
      TRIPLETS+=("$1")
      shift
      ;;
    --clean-outdated)
      CLEAN_OUTDATED=true
      shift
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 1
      ;;
  esac
done

resolve_vcpkg_executable() {
  local exe="vcpkg"
  if [[ "${OS:-}" == "Windows_NT" ]]; then
    exe="vcpkg.exe"
  fi

  if [[ -n "${VCPKG_ROOT:-}" ]] && [[ -x "${VCPKG_ROOT}/${exe}" ]]; then
    printf '%s\n' "${VCPKG_ROOT}/${exe}"
    return 0
  fi

  if command -v vcpkg >/dev/null 2>&1; then
    command -v vcpkg
    return 0
  fi

  echo "vcpkg executable not found. Set VCPKG_ROOT or add vcpkg to PATH." >&2
  exit 1
}

vcpkg_exe="$(resolve_vcpkg_executable)"

if [[ ${#TRIPLETS[@]} -eq 0 ]]; then
  if [[ "${OS:-}" == "Windows_NT" ]]; then
    TRIPLETS=("x64-windows")
  else
    TRIPLETS=("x64-linux")
  fi
fi

for triplet in "${TRIPLETS[@]}"; do
  if [[ "${CLEAN_OUTDATED}" == true ]]; then
    echo "[setup_vcpkg] Removing outdated packages for triplet '${triplet}'."
    "${vcpkg_exe}" remove --outdated --triplet "${triplet}" >/dev/null
  fi

  echo "[setup_vcpkg] Installing manifest dependencies for triplet '${triplet}'."
  "${vcpkg_exe}" install --triplet "${triplet}"
done
