#!/bin/bash
#
# This script adds a MiniSat based solver to this repository, and places the
# solver relative to its ancestor solver.

# Get parameters
PARENT_COMMIT="$1"  #  e.g. solver-minisat2.2.0
SOURCE_DIR="$2"  # e.g. ~/git/mergesat
SOLVER_NAME="$3"  # e.g. "glucose-2.0"

# This prefix is used to switch between actual solver name and git object name
declare -r GIT_PREFIX="solver-"

if [ "$PARENT_COMMIT" == "-h" ]
then
    echo "usage: $0 parent_commit source_dir solver_name"
    exit 0
fi

if [ -z "$SOURCE_DIR" ]
then
    echo "error: no solver source directory found, abort"
    exit 1
fi

if [ -z "$SOLVER_NAME" ]
then
    echo "error: no solvename found, abort"
    exit 1
fi

# Check state of destination (this) repository
check_destination_repo ()
{
    local -r COMMIT="$1"
    declare -i STATUS=0
    git show -q "$COMMIT" || STATUS=$?

    if [ "$STATUS" -ne 0 ]
    then
        echo "error: failed to spot commit $COMMIT in repository: $(pwd)"
    fi

    return "$STATUS"
}

# Check whether source dir is sufficiently close to a MiniSat solver
check_source_solver ()
{
    local -r SOURCE_DIR="$1"

    local -i SOURCE_STATUS=0

    # Files that are typically present in MiniSat
    RELEVANT_FILES=("$SOURCE_DIR/core/Solver.cc" "$SOURCE_DIR/core/SolverTypes.h" "$SOURCE_DIR/core/Dimacs.h" "$SOURCE_DIR/core/Solver.h" "$SOURCE_DIR/core/Main.cc")
    RELEVANT_FILES+=("$SOURCE_DIR/mtl/Vec.h" "$SOURCE_DIR/mtl/Alg.h" "$SOURCE_DIR/mtl/XAlloc.h" "$SOURCE_DIR/mtl/Sort.h" "$SOURCE_DIR/mtl/config.mk")
    RELEVANT_FILES+=("$SOURCE_DIR/mtl/Alloc.h" "$SOURCE_DIR/mtl/Map.h" "$SOURCE_DIR/mtl/Heap.h" "$SOURCE_DIR/mtl/IntTypes.h")
    RELEVANT_FILES+=("$SOURCE_DIR/mtl/template.mk" "$SOURCE_DIR/mtl/Queue.h")
    RELEVANT_FILES+=("$SOURCE_DIR/utils/Options.cc" "$SOURCE_DIR/utils/ParseUtils.h" "$SOURCE_DIR/utils/Options.h" "$SOURCE_DIR/utils/System.cc" "$SOURCE_DIR/utils/System.h")
    RELEVANT_FILES+=("$SOURCE_DIR/simp/SimpSolver.cc" "$SOURCE_DIR/simp/SimpSolver.h" "$SOURCE_DIR/simp/Main.cc")

    # Check whether all relevant files are in source dir
    for f in "${RELEVANT_FILES[@]}"
    do
        if [ ! -r "$f" ]
        then
            echo "warning: cannot find file $f in source solver"
        fi
    done

    # Check whether all directories are present
    for d in . core simp utils mtl
    do
        if [ ! -d "$SOURCE_DIR/$d" ]
        then
            echo "error: cannot find directory $f in source solver $SOURCE_DIR"
            SOURCE_STATUS=1
        fi
    done

    return "$SOURCE_STATUS"
}

copy_from_source ()
{
    local SOURCE_DIR="$1"
    local -i RSYNC_STATUS=0

    # Copy all relevant directories
    for d in core simp utils mtl
    do
        echo "Copying directory $d"
        rsync -avz "$SOURCE_DIR/$d/" "$d/" || RSYNC_STATUS=1
    done
    rsync -avz "$SOURCE_DIR/mtl/template.mk" "mtl/"

    # If there is a LICENSE, README and Makefile file, also copy it
    rsync -avz "$SOURCE_DIR/LICENSE" "."
    rsync -avz "$SOURCE_DIR/README" "."
    rsync -avz "$SOURCE_DIR/Makefile" "."

    return "$RSYNC_STATUS"
}

commit_solver ()
{

    local -r SOLVER_NAME="$1"
    local -r PARENT_COMMIT="$2"

    # Create a commit for the relevant files
    git add LICENSE || true
    git add README || true
    git add Makefile || true
    # Only add header and source files
    for d in core simp utils mtl
    do
        git add "$d/*.cc"
        git add "$d/*.h"
        git add "$d/Makefile"
    done
    git add "mtl/template.mk"

    # Commit the changes, use "signed-off by" from actor
    echo -e "solvers: add $SOLVER_NAME\n\nAdd solver $SOLVER_NAME, and add it to\nthe repository, and use as parent\n$PARENT_COMMIT" | git commit -s -F '-'
    COMMIT_STATUS="${PIPESTATUS[1]}"

    if [ "$COMMIT_STATUS" -ne 0 ]
    then
        echo "error: committing the change failed"
    fi
    return "$COMMIT_STATUS"
}

check_compilation ()
{
    local STATUS=0
    for d in simp core
    do
        pushd "$d"
        echo "Compile: $d - debug"
        CFLAGS="-Wall -Wno-parentheses -fpermissive" make d -j MROOT=$(readlink -e ..) || STATUS=$?
        echo "Compile: $d - release"
        CFLAGS="-Wall -Wno-parentheses -fpermissive" make r -j MROOT=$(readlink -e ..) || STATUS=$?
        echo "Compile: $d - static"
        CFLAGS="-Wall -Wno-parentheses -fpermissive" make s -j MROOT=$(readlink -e ..) || STATUS=$?
        echo "Compile: $d - clean"
        CFLAGS="-Wall -Wno-parentheses -fpermissive" make clean -j MROOT=$(readlink -e ..) || STATUS=$?
        popd
    done

    return $STATUS
}

# Create a tag for the solver
tag_commit ()
{
    local -r SOLVER_NAME="$1"
    local -r TAG_NAME=$(echo "solver-$SOLVER_NAME" | tr ' ' '_')
    git tag -m "Created tag from parent $PARENT_COMMIT" "$TAG_NAME"
}

# Make sure we find the directory again
SOURCE_DIR="$(readlink -e $SOURCE_DIR)"

# Get to repository base directory
cd $(dirname "${BASH_SOURCE[0]}")/..

# Make sure the commit exists
check_destination_repo "$PARENT_COMMIT" || exit 1

check_source_solver "$SOURCE_DIR"

# Create branch for the new solver, prefix its name
declare -i CHECKOUT_STATUS=0
git checkout -b "${GIT_PREFIX}${SOLVER_NAME}" "$PARENT_COMMIT" || CHECKOUT_STATUS=$?
if [ "$CHECKOUT_STATUS" -ne 0 ]
then
    echo "error: failed to create branch for new solver: $SOLVER_NAME"
    exit 1
fi

copy_from_source "$SOURCE_DIR" || exit 1

# Enforce style
./tools/enforce-style.sh

# Check whether solver compiles
check_compilation || exit 1

# Commit the solver
commit_solver "$SOLVER_NAME" "$PARENT_COMMIT"

# Tag commit
# TODO: allow to control tag via CLI
# tag_commit "$SOLVER_NAME" || exit 1

echo "All done"
exit 0