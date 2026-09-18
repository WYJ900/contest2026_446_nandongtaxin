#!/usr/bin/env bash
set -euo pipefail

CHECK_ONLY=0
if [[ "${1:-}" == "--check" ]]; then
  CHECK_ONLY=1
fi

TEAM_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKSPACE_ROOT="$(cd "$TEAM_ROOT/.." && pwd)"

apply_patch_file() {
  local repo_rel="$1"
  local patch_name="$2"
  local repo="$WORKSPACE_ROOT/$repo_rel"
  local patch="$TEAM_ROOT/patches/$patch_name"

  [[ -d "$repo/.git" || -f "$repo/.git" ]] || {
    echo "Missing Git repository: $repo" >&2
    return 1
  }

  if git -C "$repo" apply --check "$patch" 2>/dev/null; then
    if [[ "$CHECK_ONLY" == 1 ]]; then
      echo "READY patch $repo_rel"
    else
      git -C "$repo" apply "$patch"
      echo "APPLIED patch $repo_rel"
    fi
  elif git -C "$repo" apply --reverse --check "$patch" 2>/dev/null; then
    echo "ALREADY patch $repo_rel"
  else
    echo "Patch conflicts with $repo_rel: $patch_name" >&2
    return 1
  fi
}

install_overlay() {
  local src_rel="$1"
  local dst_rel="$2"
  local src="$TEAM_ROOT/overlays/$src_rel"
  local dst="$WORKSPACE_ROOT/$dst_rel"

  [[ -f "$src" ]] || { echo "Missing overlay: $src" >&2; return 1; }
  if [[ -f "$dst" ]]; then
    cmp -s "$src" "$dst" || {
      echo "Overlay conflicts with existing file: $dst" >&2
      return 1
    }
    echo "ALREADY overlay $dst_rel"
  elif [[ "$CHECK_ONLY" == 1 ]]; then
    echo "READY overlay $dst_rel"
  else
    mkdir -p "$(dirname "$dst")"
    cp "$src" "$dst"
    echo "COPIED overlay $dst_rel"
  fi
}

apply_patch_file apps apps.patch
apply_patch_file nuttx nuttx.patch
apply_patch_file packages/ai_agent packages-ai-agent.patch
apply_patch_file vendor/sifli vendor-sifli.patch

install_overlay apps/system/libuv/nuttx_threadpool.c apps/system/libuv/nuttx_threadpool.c
install_overlay vendor/sifli/chips/drivers/epic/drv_epic.c vendor/sifli/chips/drivers/epic/drv_epic.c
install_overlay vendor/sifli/chips/drivers/epic/drv_epic.h vendor/sifli/chips/drivers/epic/drv_epic.h
install_overlay vendor/sifli/chips/drivers/epic/drv_epic_priv.h vendor/sifli/chips/drivers/epic/drv_epic_priv.h

echo 'Integration check completed.'
