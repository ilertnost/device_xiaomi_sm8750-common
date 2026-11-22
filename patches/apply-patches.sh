#!/bin/bash
#
# SPDX-License-Identifier: Apache-2.0
#
# Applies the source patches carried by the device trees on top of a synced
# tree. Called from the device's vendorsetup.sh, so it runs on every
# `source build/envsetup.sh` when the patch-based manifest is active.
#
# Usage: apply-patches.sh <patches-dir>...
#
# Every <patches-dir> (relative to the top of the tree) holds one directory per
# project, named after the project path with '/' replaced by '_', containing
# git format-patch files. If the project path contains an underscore (for
# example external/rust/rutabaga_gfx), put the real project path in a
# `.project` file inside that directory. Directories starting with '_' are
# backups and are ignored.
#
# The numbers of a project's patches form a single sequence across all patch
# directories: they are applied in file name order, and patch directories are
# only used to break ties, in the order given.
#
# For every project the result of applying all of its patches to the manifest
# revision is first computed in a temporary index:
#  - a working tree that already matches it is left alone,
#  - a clean checkout of the manifest revision gets the patches with `git am`,
#  - anything else, including a checkout with an am, rebase, merge, cherry-pick
#    or revert in progress, is reported and left untouched.
#

TOP=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

log() {
    echo "apply-patches: $*"
}

projects=()
declare -A project_patches

dir_index=0
for dir in "$@"; do
    dir_index=$((dir_index + 1))
    if [ ! -d "$TOP/$dir" ]; then
        log "$dir: not found, skipping"
        continue
    fi
    for project_dir in "$TOP/$dir"/*/; do
        name=$(basename "$project_dir")
        case "$name" in
            _*|.*) continue ;;
        esac
        if [ -f "$project_dir/.project" ]; then
            project=$(head -n 1 "$project_dir/.project")
        else
            project=${name//_//}
        fi
        if [ -z "${project_patches[$project]+set}" ]; then
            projects+=("$project")
            project_patches[$project]=""
        fi
        for patch in "$project_dir"*.patch; do
            [ -f "$patch" ] &&
                project_patches[$project]+="$(basename "$patch")"$'\t'"$dir_index"$'\t'"$patch"$'\n'
        done
    done
done

up_to_date=0
for project in "${projects[@]}"; do
    git_dir=(git -C "$TOP/$project")
    mapfile -t patches < <(printf '%s' "${project_patches[$project]}" | LC_ALL=C sort -t $'\t' -k1,1 -k2,2n | cut -f3)
    [ ${#patches[@]} -eq 0 ] && continue

    # git am needs the format-patch headers; the author also becomes the
    # committer below.
    missing=()
    for patch in "${patches[@]}"; do
        sed '/^$/q' "$patch" | grep -q '^From: .* <.*>$' || missing+=("$(basename "$patch")")
    done
    if [ ${#missing[@]} -gt 0 ]; then
        log "$project: no format-patch From: header in ${missing[*]}, skipping"
        continue
    fi

    if ! "${git_dir[@]}" rev-parse --git-dir >/dev/null 2>&1; then
        log "$project: not a git checkout, skipping"
        continue
    fi

    base=$("${git_dir[@]}" for-each-ref --count=1 --format='%(objectname)' refs/remotes/m/)
    if [ -z "$base" ]; then
        log "$project: no manifest revision (refs/remotes/m/*), skipping"
        continue
    fi

    # Result of the whole series on top of the manifest revision.
    safe=${project//\//_}
    index="$TMP/${safe}.index"
    "${git_dir[@]}" read-tree --index-output="$index" "$base"
    applies=true
    for patch in "${patches[@]}"; do
        if ! GIT_INDEX_FILE="$index" "${git_dir[@]}" apply --cached "$patch" 2>"$TMP/err"; then
            log "$project: $(basename "$patch") does not apply to the manifest revision:"
            sed 's/^/    /' "$TMP/err"
            applies=false
            break
        fi
    done
    $applies || continue
    expected=$(GIT_INDEX_FILE="$index" "${git_dir[@]}" write-tree)

    if "${git_dir[@]}" diff --quiet "$expected" --; then
        up_to_date=$((up_to_date + 1))
        continue
    fi

    if [ "$("${git_dir[@]}" rev-parse 'HEAD^{tree}')" != "$("${git_dir[@]}" rev-parse "$base^{tree}")" ] ||
            ! "${git_dir[@]}" diff --quiet HEAD -- ||
            ! "${git_dir[@]}" diff --cached --quiet --; then
        log "$project: local changes differ from the patches, leaving it untouched"
        continue
    fi

    # `git am --abort` below must only ever undo our own session.
    in_progress=
    for state in rebase-apply rebase-merge MERGE_HEAD CHERRY_PICK_HEAD REVERT_HEAD; do
        if [ -e "$("${git_dir[@]}" rev-parse --path-format=absolute --git-path "$state")" ]; then
            in_progress=$state
            break
        fi
    done
    if [ -n "$in_progress" ]; then
        log "$project: a git operation is in progress ($in_progress), leaving it untouched"
        continue
    fi

    start=$("${git_dir[@]}" rev-parse HEAD)
    for patch in "${patches[@]}"; do
        # Commit as the patch author with its date so the result is the same
        # on every machine and needs no git identity.
        from=$(sed -n '/^$/q; s/^From: //p' "$patch")
        email=${from##*<}
        if ! GIT_COMMITTER_NAME="${from% <*}" GIT_COMMITTER_EMAIL="${email%>}" \
                "${git_dir[@]}" am -q --committer-date-is-author-date "$patch" >"$TMP/err" 2>&1; then
            log "$project: failed to apply $(basename "$patch"):"
            sed 's/^/    /' "$TMP/err"
            "${git_dir[@]}" am --abort >/dev/null 2>&1
            "${git_dir[@]}" reset -q --hard "$start"
            continue 2
        fi
    done
    log "$project: applied ${#patches[@]} patch(es)"
done

[ $up_to_date -gt 0 ] && log "$up_to_date project(s) already up to date"
exit 0
