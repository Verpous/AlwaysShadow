#! /bin/bash

confirm() {
    local confirm
    read -ep "$1 " confirm # Add a space at the end.
    [[ "$confirm" == *([[:space:]])[yY]* ]]
}

write_if_diff() {
    local tmp="$(mktemp)"
    cat > "$tmp"

    if [[ ! -f "$1" ]] || ! diff -q "$tmp" "$1"; then
        cp -- "$tmp" "$1"
    fi

    rm -- "$tmp"
}

latest_release() {
    # gh release view makes it easier to obtain the latest release, but it doesn't show draft releases.
    gh release list --json "$1" --jq ".[0] | .$1"
}

# User runs "make_utils.sh <func name> <func args>", and we run the function with the args.
"$@"