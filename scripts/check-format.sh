#!/bin/sh

SRC_DIR=$(dirname "$0")/..

exec >& /dev/stderr

set -x
env

if [ -z "$CI_MERGE_REQUEST_DIFF_BASE_SHA" ]; then
    UPSTREAM_BRANCH="$(git rev-parse --abbrev-ref --symbolic-full-name @{u})"
else
    UPSTREAM_BRANCH="$CI_MERGE_REQUEST_DIFF_BASE_SHA"
fi

cd "$SRC_DIR"

cp scripts/uncrustify.cfg scripts/latest-uncrustify.cfg

git diff --quiet
DIRTY_TREE="$?"

if [ "$DIRTY_TREE" -ne 0 ]; then
    git stash
    git stash apply
fi

find -name '*.[ch]' -exec uncrustify -q -c scripts/latest-uncrustify.cfg --replace {} \;

echo > after
find -name '*.[ch]' -exec git diff -- {} \; >> after

git reset --hard $UPSTREAM_BRANCH
find -name '*.[ch]' -exec uncrustify -q -c scripts/latest-uncrustify.cfg --replace {} \;

echo > before
find -name '*.[ch]' -exec git diff -- {} \; >> before

interdiff --no-revert-omitted before after > diff

if [ -n "$(cat diff)" ]; then
    echo "Uncrustify found style abnormalities" 2>&1
    cat diff
    exit 1
fi

git reset --hard HEAD@{1}

if [ "$DIRTY_TREE" -ne 0 ]; then
    git stash pop
fi

echo "No new style abnormalities found by uncrustify!"
exit 0

