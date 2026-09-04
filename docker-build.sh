#!/bin/bash
# Docker build for wLaunchELF (Chinese localized build)
# Mirrors the official upstream CI (.github/workflows/compile.yml):
#   - current official toolchain image (ps2dev/ps2dev on Docker Hub)
#   - same dependency install and PS2SDK source resolution
#   - official R3Z variant profile: psx + no-ds34 + all storage
# Note: the upstream ghcr.io/ps2homebrew image is not publicly pullable from
# this network ("denied"), so the equally official Docker Hub toolchain image
# is used instead. No toolchain patches are needed with this image.
set -e
cd "$(dirname "$0")"

IMAGE="ps2dev/ps2dev:latest"

# pwd -W returns a Windows-style path under Git Bash (MSYS), which docker accepts
VOL=$(pwd -W 2>/dev/null || pwd)

# Prevent MSYS (Git Bash) from rewriting -w /project into a Windows path
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL="*"

docker run --rm --entrypoint /bin/sh -v "$VOL:/project" -w /project "$IMAGE" -c '
  set -e

  # 1. Build dependencies (same set as upstream CI; fallback covers pkg renames)
  apk add --no-cache make git zip gcc musl-dev gmp mpfr4 mpc1 \
    || apk add --no-cache make git zip gcc musl-dev gmp libmpfr libmpc

  # Make git work on the mounted volume (ownership differs inside container)
  git config --global --add safe.directory /project

  # 2. Resolve PS2SDK source tree for local IOP module builds (same logic as CI)
  if [ -n "${PS2SDKSRC:-}" ] && [ -f "$PS2SDKSRC/Defs.make" ] && [ -f "$PS2SDKSRC/iop/Rules.make" ]; then
    echo "Using PS2SDKSRC=$PS2SDKSRC"
  elif [ -n "${PS2SDK:-}" ] && [ -f "$PS2SDK/Defs.make" ] && [ -f "$PS2SDK/iop/Rules.make" ]; then
    echo "Using PS2SDK source tree at $PS2SDK"
    export PS2SDKSRC="$PS2SDK"
  else
    git clone --depth 1 https://github.com/ps2dev/ps2sdk.git /tmp/ps2sdk-src
    export PS2SDKSRC=/tmp/ps2sdk-src
  fi

  # FatFs: switch LFN API encoding to UTF-8 so Chinese filenames on
  # USB/MX4SIO/ATA(exFAT) survive the IOP->EE handoff (default OEM CP mangles
  # them to "?"). Then BUILD the patched bdmfs_fatfs module and vendor it
  # into iop/__precompiled/ — embed.make prefers that copy, so the main make
  # (and the GitHub CI) always embed this UTF-8 LFN build instead of the
  # prebuilt module shipped with the toolchain image.
  git clone --depth 1 -b iop-r0.16 https://github.com/fjtrujy/FatFs.git \
      /tmp/ps2sdk-src/common/external_deps/fatfs
  sed -i "s/^#define[[:space:]]*FF_LFN_UNICODE[[:space:]].*/#define FF_LFN_UNICODE 2/" \
      /tmp/ps2sdk-src/common/external_deps/fatfs/source/include/ffconf.h
  grep "^#define FF_LFN_UNICODE 2" \
      /tmp/ps2sdk-src/common/external_deps/fatfs/source/include/ffconf.h
  mkdir -p /tmp/fatfs-out /tmp/fatfs-obj
  make -C /tmp/ps2sdk-src/iop/fs/bdmfs_fatfs \
      PS2SDKSRC=/tmp/ps2sdk-src PS2SDK=/tmp/ps2sdk-src \
      IOP_BIN_DIR=/tmp/fatfs-out/ IOP_OBJS_DIR=/tmp/fatfs-obj/ \
      IOP_BIN=bdmfs_fatfs.irx
  # this ps2sdk Makefile drops the IRX in the module dir, not IOP_BIN_DIR
  if [ -f /tmp/fatfs-out/bdmfs_fatfs.irx ]; then
    cp /tmp/fatfs-out/bdmfs_fatfs.irx iop/__precompiled/bdmfs_fatfs.irx
  else
    cp /tmp/ps2sdk-src/iop/fs/bdmfs_fatfs/bdmfs_fatfs.irx iop/__precompiled/bdmfs_fatfs.irx
  fi
  echo "=== Vendored UTF-8 LFN bdmfs_fatfs.irx ==="
  ls -la iop/__precompiled/bdmfs_fatfs.irx

  # 3. Build with the official R3Z profile flags (psx + no-ds34 + all)
  chmod +x scripts/ci/resolve_make_args.sh
  FLAGS="$(scripts/ci/resolve_make_args.sh psx no-ds34 all 0 0)"
  echo "Resolved make flags: $FLAGS"
  make rebuild $FLAGS

  # 4. Loader layout guard (same as CI)
  make -C loader check-layout DEBUG=0
'

echo "=== Build products ==="
ls -la UNC-BOOT-*.ELF BOOT-*.ELF 2>/dev/null
