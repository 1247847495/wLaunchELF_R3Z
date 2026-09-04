#!/bin/bash
# Docker build for wLaunchELF (Chinese localized build)
# Mirrors the official upstream CI (.github/workflows/compile.yml) exactly:
#   - official toolchain image ghcr.io/ps2homebrew/ps2homebrew:main
#   - same dependency install, PS2SDKSRC resolution, make flags
#   - R3Z variant profile: psx + no-ds34 + all storage
# No toolchain patches are applied: the official image works out of the box.
set -e
cd "$(dirname "$0")"

IMAGE="ghcr.io/ps2homebrew/ps2homebrew:main"

# pwd -W returns a Windows-style path under Git Bash (MSYS), which docker accepts
VOL=$(pwd -W 2>/dev/null || pwd)

# Prevent MSYS (Git Bash) from rewriting -w /project into a Windows path
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL="*"

docker run --rm -v "$VOL:/project" -w /project "$IMAGE" bash -exc '
  # 1. Same dependencies as upstream CI
  if command -v apk >/dev/null 2>&1; then
    apk add --no-cache make git zip gcc musl-dev gmp mpfr4 mpc1
  elif command -v apt-get >/dev/null 2>&1; then
    apt-get update
    DEBIAN_FRONTEND=noninteractive apt-get install -y make git zip gcc libc6-dev libgmp10 libmpfr6 libmpc3
  else
    echo "No supported package manager found." >&2
    exit 1
  fi

  # Make git work on the mounted volume (ownership differs inside container)
  git config --global --add safe.directory /project

  # 2. Resolve PS2SDK source tree for local IOP module builds (same as CI)
  if [ -n "${PS2SDKSRC:-}" ] && [ -f "$PS2SDKSRC/Defs.make" ] && [ -f "$PS2SDKSRC/iop/Rules.make" ]; then
    echo "Using PS2SDKSRC=$PS2SDKSRC"
  elif [ -n "${PS2SDK:-}" ] && [ -f "$PS2SDK/Defs.make" ] && [ -f "$PS2SDK/iop/Rules.make" ]; then
    echo "Using PS2SDK source tree at $PS2SDK"
    export PS2SDKSRC="$PS2SDK"
  else
    git clone --depth 1 https://github.com/ps2dev/ps2sdk.git /tmp/ps2sdk-src
    export PS2SDKSRC=/tmp/ps2sdk-src
  fi

  # 3. Build with the official R3Z profile flags (psx + no-ds34 + all)
  chmod +x scripts/ci/resolve_make_args.sh
  FLAGS="$(scripts/ci/resolve_make_args.sh psx no-ds34 all 0 0)"
  echo "Resolved make flags: $FLAGS"
  make rebuild $FLAGS

  # 4. Same loader layout guard as CI
  make -C loader check-layout DEBUG=0
'

echo "=== Build products ==="
ls -la UNC-BOOT-*.ELF BOOT-*.ELF 2>/dev/null
