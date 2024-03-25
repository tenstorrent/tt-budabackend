#!/bin/sh

# If clang-format is not present on the system, don't block the commit, but throw a warning.
if ! command -v clang-format >/dev/null 2>&1
then
        echo "\
Warning: clang-format is not installed on this system, skipping automatic formatting. \
Note that CI will fail if your c++ code is not properly formatted."
        exit 0
fi

# The first number that appears in clang-format --version output is the major version.
clang_format_version=$(clang-format --version | awk -F'[^0-9]+' '{ print $2 }')
required_version="19"

# Warn the user if using mismatched clang-format version.
if [ "$clang_format_version" != "$required_version" ]; then
    echo "Warning: clang-format version $required_version is used in CI, but found version $clang_format_version."
fi

# Note that it is tricky to automatically change files and stage them in a pre-commit hook,
# since there might be changes to the same file that are not staged yet, and thus we might
# get all sorts of merge conflicts. 
# Also, automatically changing code might be confusing to the user.
# Instead, block the commit and warn the user to format the code manually
clangformatdiff=$(git clang-format --staged --diff -q)
if [ "$clangformatdiff" != "" ]  
then
    echo "Commit blocked, files not formatted correctly (git clang-format --staged --diff):"
    git clang-format --staged --diff
    echo "If you want to manually format a piece of code, you can add \"// clang-format off\" and \"// clang-format on\" around it."
    echo "To fix the formatting you can run the following command and then stage the changes afterwards:"
    echo "git clang-format --staged"
    exit 1
fi
