#!/usr/bin/env sh
set -eu

out="${1:-src/version.h}"
major="${2:-0}"
minor="${3:-1}"
patch="${4:-0}"
abi="${5:-1}"

git_hash="$(git rev-parse --short HEAD 2>/dev/null || echo nogit)"
if ! git diff --quiet --ignore-submodules -- 2>/dev/null; then
  git_hash="${git_hash}-dirty"
fi

build_date="$(date -u +"%Y-%m-%dT%H:%M:%SZ" 2>/dev/null || echo unknown)"
ver="${major}.${minor}.${patch}"
full="${ver}-g${git_hash}"

cat > "$out" <<EOF
#ifndef _VERSION_H
#define _VERSION_H

#define KERNEL_VERSION_MAJOR ${major}
#define KERNEL_VERSION_MINOR ${minor}
#define KERNEL_VERSION_PATCH ${patch}
#define KERNEL_VERSION_ABI ${abi}

#define KERNEL_VERSION_STR "${ver}"
#define KERNEL_VERSION_GIT "${git_hash}"
#define KERNEL_BUILD_DATE_UTC "${build_date}"
#define KERNEL_VERSION_FULL "${full}"

#endif
EOF
