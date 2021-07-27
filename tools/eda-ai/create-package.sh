#!/bin/bash
#

# locate the script to be able to call related scripts
SCRIPT=$(readlink -e "$0")
SCRIPTDIR=$(dirname "$SCRIPT")

declare -r START_DIR="$PWD"

# fail on error
set -e

# get work directory
trap '[ -d "$WORKDIR" ] && rm -rf "$WORKDIR"' EXIT
WORKDIR=$(mktemp -d)

# create code dir
CODE_DIR="$WORKDIR"/code
mkdir -p "$CODE_DIR"

# copy full content into code directory
git clone "$SCRIPTDIR"/../.. "$CODE_DIR"/mergesat

# get dependencies, if not there already: Caution: this can drop state
pushd "$CODE_DIR"/mergesat
git submodule update --init --recursive
git clean -xfd || true
git submodule foreach --recursive git clean -xfd || true
popd

# create required links in root directory
pushd "$WORKDIR"
ln -s code/mergesat/license.txt license.txt
ln -s code/mergesat/README readme.txt
cp -r "$SCRIPTDIR"/build.sh .
popd

# log git version
pushd "$SCRIPTDIR"
echo "Package git version:" > "$WORKDIR"/VERSION
git describe || git show -s --pretty=oneline >> "$WORKDIR"/VERSION
GIT_VERSION="$(git describe 6> /dev/null || true)"
[ -n "$GIT_VERSION" ] && GIT_VERSION="-$GIT_VERSION"
[ -z "$GIT_VERSION" ] && GIT_VERSION="-unknown"
echo "Submodules:" >> "$WORKDIR"/VERSION
git submodule status >> "$WORKDIR"/VERSION
echo "Git remotes:" >> "$WORKDIR"/VERSION
git remote -v | grep origin || true >> "$WORKDIR"/VERSION
popd

declare -r ZIP_NAME="mergesat${GIT_VERSION}.zip"

# remove git history, as it will be large, and other files
pushd "$CODE_DIR"
rm -rf mergesat/.git
rm -f "$ZIP_NAME"
rm -rf binary/*
rm -rf doc/description
popd

# zip to overall package
pushd "$WORKDIR"
zip -r -y -9 "$ZIP_NAME" *

# copy archive back to workdir
cp "$ZIP_NAME" "$START_DIR"

# leave work directory
popd
