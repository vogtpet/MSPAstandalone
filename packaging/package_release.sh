#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Package the standalone MSPA binaries for a GitHub release.
#
# Usage: ./packaging/package_release.sh <version>
#
#   <version> is the release version without the leading "v", e.g. 2.4.
#
# The five mspa_dist_* directories must be present in the repository root.
# They are created by ./deploy.sh <target> on the respective machines and
# are ignored by git. The script creates release/mspa-<version>/ with one
# archive per platform and a SHA256SUMS file.
# ---------------------------------------------------------------------------
set -euo pipefail

version="${1:-}"
if [ -z "$version" ]; then
    echo "Usage: $0 <version>   (for example: $0 2.4)" >&2
    exit 1
fi

version="${version#v}"
case "$version" in
    ''|*[!0-9.]*)
        echo "ERROR: invalid version '$version'." >&2
        exit 1
        ;;
esac

cd "$(dirname "${BASH_SOURCE[0]}")/.."

if ! command -v zip >/dev/null 2>&1; then
    echo "ERROR: 'zip' is required to create the Windows archives." >&2
    exit 1
fi

out_dir="release/mspa-$version"
out_abs="$(pwd)/$out_dir"
rm -rf "$out_dir"
mkdir -p "$out_dir"

staging="$(mktemp -d)"
trap 'rm -rf "$staging"' EXIT

entries=(
    "linux-x86_64:mspa_dist_linux"
    "linux-aarch64:mspa_dist_linux_ARM"
    "macos-arm64:mspa_dist_macOS_ARM"
    "windows-x86_64:mspa_dist_windows"
    "windows-arm64:mspa_dist_windows_ARM"
)

echo "Packaging MSPA $version"

for entry in "${entries[@]}"; do
    platform="${entry%%:*}"
    dist="${entry#*:}"

    if [ ! -d "$dist" ]; then
        echo "ERROR: $dist not found." >&2
        echo "       Build the corresponding target with ./deploy.sh and copy" >&2
        echo "       the directory into $(pwd)." >&2
        exit 1
    fi
    if ! compgen -G "$dist/mspa*" >/dev/null; then
        echo "ERROR: no mspa binary found in $dist." >&2
        exit 1
    fi

    name="mspa-$version-$platform"
    top="$staging/$name"
    mkdir -p "$top"
    cp -R "$dist"/. "$top"/
    cp LICENSE README.md CITATION.cff "$top"/
    [ -d docs ] && cp -R docs "$top"/

    # Normalise permissions so the archive is usable by whoever extracts it
    chmod -R a+rX "$top"
    chmod 755 "$top"/mspa* 2>/dev/null || true

    case "$platform" in
        windows-*)
            (cd "$staging" && zip -qr "$out_abs/$name.zip" "$name")
            echo "  $name.zip"
            ;;
        *)
            (cd "$staging" && tar -czf "$out_abs/$name.tar.gz" "$name")
            echo "  $name.tar.gz"
            ;;
    esac

    rm -rf "$top"
done

rm -rf "$staging"
trap - EXIT

if command -v sha256sum >/dev/null 2>&1; then
    (cd "$out_dir" && sha256sum ./*.tar.gz ./*.zip > SHA256SUMS)
else
    (cd "$out_dir" && shasum -a 256 ./*.tar.gz ./*.zip > SHA256SUMS)
fi

echo
echo "Release files:"
ls -lh "$out_dir"
echo
echo "Verify with:  (cd $out_dir && sha256sum -c SHA256SUMS)"
