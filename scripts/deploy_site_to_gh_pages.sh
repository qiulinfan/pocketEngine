#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SITE_DIR="$ROOT_DIR/.site"
WORKTREE_DIR="$ROOT_DIR/.gh-pages-worktree"
BRANCH_NAME="gh-pages"

if [ ! -d "$SITE_DIR" ]; then
    echo "error: .site does not exist. Run 'make site-build' first." >&2
    exit 1
fi

git -C "$ROOT_DIR" worktree remove --force "$WORKTREE_DIR" >/dev/null 2>&1 || true

if git -C "$ROOT_DIR" rev-parse --verify "origin/$BRANCH_NAME" >/dev/null 2>&1; then
    git -C "$ROOT_DIR" worktree add -B "$BRANCH_NAME" "$WORKTREE_DIR" "origin/$BRANCH_NAME"
else
    git -C "$ROOT_DIR" worktree add -B "$BRANCH_NAME" "$WORKTREE_DIR" HEAD
fi

find "$WORKTREE_DIR" -mindepth 1 -maxdepth 1 ! -name '.git' -exec rm -rf {} +
rsync -av --delete --exclude '.git' "$SITE_DIR"/ "$WORKTREE_DIR"/
touch "$WORKTREE_DIR/.nojekyll"

git -C "$WORKTREE_DIR" add --all
if git -C "$WORKTREE_DIR" diff --cached --quiet; then
    echo "gh-pages is already up to date."
else
    git -C "$WORKTREE_DIR" commit -m "Deploy documentation site"
    git -C "$WORKTREE_DIR" push origin "$BRANCH_NAME" --force
fi

git -C "$ROOT_DIR" worktree remove --force "$WORKTREE_DIR"
