#!/bin/bash

# Instructions
# 1. Make a copy of this script to avoid itself from getting deleted during `rm -rf scripts`
# 2. Clone/checkout the desired SHA of the budabackend repo to be staged to Github
#   - git clone git@yyz-gitlab.local.tenstorrent.com:tenstorrent/budabackend.git bbe_staging
#   - cd bbe_staging && git checkout <desired-sha>
# 3. Perform manual checks to ensure the branch is in the desired state
#   - confidential information should not be exposed
#   - secrets and keys should not be exposed
# 4. Release it to the wild
#   - git push


# debug only
# set -x

original_branch=$(git branch --show-current)
original_commit=$(git rev-parse HEAD)

# ----------------------------------------
# Store all the existing submodule pointers
# ----------------------------------------
git submodule sync
git submodule update --init --recursive
get_submodule_hash() {
    local submodule=$1
    if [ -d "$submodule" ]; then
        cd $submodule > /dev/null 2>&1
        local commit=$(git rev-parse HEAD)
        cd - > /dev/null 2>&1
        echo "$commit"  # Return the commit hash as a string
    fi
}
declare -A submodule_hash_map
submodule_hash_map["umd"]=$(get_submodule_hash umd)
submodule_hash_map["third_party/sfpi"]=$(get_submodule_hash third_party/sfpi)
submodule_hash_map["src/ckernels/sfpi"]=$(get_submodule_hash src/ckernels/sfpi)
submodule_hash_map["third_party/pybind11"]=$(get_submodule_hash third_party/pybind11)
submodule_hash_map["third_party/tt_llk_grayskull"]=$(get_submodule_hash third_party/tt_llk_grayskull)
submodule_hash_map["third_party/tt_llk_wormhole_b0"]=$(get_submodule_hash third_party/tt_llk_wormhole_b0)

# These files contain information we don't want to publically release
rm ci/docker_builder/special.Dockerfile
rm -rf ci
rm -rf bin
rm .gitlab-ci*
rm -rf scripts
rm -rf src/firmware/riscv/targets/erisc
rm -rf device/legacy/device

# Do not expose confidential repo name in files
for f in $(git grep -l "confidential_tenstorrent_modules") ; do sed -i '/TT_MODULES ?= third_party\/confidential_tenstorrent_modules/c\TT_MODULES=' ${f} ; done

# ----------------------------------------
# Reinitialize the repo
# ----------------------------------------
rm -rf .git
git init
git add .

# ----------------------------------------
# Filter out undesired files
# ----------------------------------------
echo "\n\n# Exclude LFS files to keep the public repo small" >> .gitignore
git lfs ls-files --all | awk '{print "rm " $3}' | bash
rm .gitattributes
touch .gitattributes
git add .gitattributes

# ----------------------------------------
# Remap the submodules to public repos
# ----------------------------------------
remove_submodule() {
    echo "Removing $1 ..."
    local submodule=$1
    local removal_keyword=$2
    git submodule deinit -f -- $submodule
    git rm -r --cached $submodule
    rm -rf $submodule
    # if removal keyword is provided, remove it from .gitmodules
    if [ ! -z "$removal_keyword" ]; then
        grep -v "$removal_keyword" .gitmodules > .gitmodules.tmp
        mv .gitmodules.tmp .gitmodules
    fi
}
remap_submodule() {
    local submodule=$1
    local url=$2
    local commit=${submodule_hash_map[$submodule]}
    remove_submodule $submodule ""
    echo "Remapping $submodule to $url at commit $commit..."
    git submodule add $url $submodule
    cd $submodule
    git checkout $commit
    cd -
}

remap_submodule umd https://github.com/tenstorrent/tt-umd

# Depending on commit, 2 locations of SFPI may exist
if [ -d "third_party/sfpi" ]; then
    remap_submodule third_party/sfpi https://github.com/tenstorrent-metal/sfpi-rel
fi
if [ -d "src/ckernels/sfpi" ]; then
    remap_submodule src/ckernels/sfpi https://github.com/tenstorrent-metal/sfpi-rel
fi

# Depending on commit, llk may be a submodule
if [ -d "third_party/tt_llk_grayskull" ]; then
    remap_submodule third_party/tt_llk_grayskull https://github.com/tenstorrent/tt-llk-gs
fi
if [ -d "third_party/tt_llk_wormhole_b0" ]; then
    remap_submodule third_party/tt_llk_wormhole https://github.com/tenstorrent/tt-llk-wh-b0
fi

remap_submodule third_party/pybind11 https://github.com/pybind/pybind11
remove_submodule third_party/confidential_tenstorrent_modules confidential_tenstorrent_modules
remove_submodule third_party/confidential_keys confidential_keys

# git submodule sync
# git submodule update --init --recursive

# ----------------------------------------
# One off fixups
# ----------------------------------------

# FIXME: remove the exclusion from .gitignore
grep -v "\/\*.yaml" .gitignore > .gitignore.tmp
mv .gitignore.tmp .gitignore
git add *.yaml

# ----------------------------------------
# Remaining steps, commit, branch, remote
# ----------------------------------------

git add -u
git commit -m "Snapshot of budabackend branch $original_branch, squashed from commit $original_commit"
git branch -m bbe_$original_commit
git remote add origin git@github.com:tenstorrent/tt-budabackend.git

echo "-------------------------------------------------------------------------------"
echo "Completed staging branch bbe_$original_commit via the following commit:"
echo "$(git show -s)"
echo "-------------------------------------------------------------------------------"
echo "Next steps:"
echo "1. Manually review that current branch is in the desired state"
echo "2. git push"
echo "-------------------------------------------------------------------------------"
