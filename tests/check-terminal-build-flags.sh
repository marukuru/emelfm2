#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
check_dir=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-build-flags.XXXXXX")
trap 'rm -rf "$check_dir"' EXIT HUP INT TERM
cat > "$check_dir/pkg-config" <<'WRAPPER'
#!/bin/sh
for argument do
    case "$argument" in
        vte|vte-2.91)
            printf '%s\n' "$argument" >> "$E2_VTE_PROBES"
            exit 1 ;;
    esac
done
exec pkg-config "$@"
WRAPPER
chmod +x "$check_dir/pkg-config"
export E2_VTE_PROBES="$check_dir/probes"
for gtk in 2 3; do
    if [ "$gtk" = 2 ]; then gtk2=1; gtk3=0; else gtk2=0; gtk3=1; fi
    make -s help WITH_GTK2="$gtk2" WITH_GTK3="$gtk3" WITH_VTE=0 \
        PKG_CONFIG="$check_dir/pkg-config" >"$check_dir/output" 2>&1
    test ! -e "$check_dir/probes"
    if make -s help WITH_GTK2="$gtk2" WITH_GTK3="$gtk3" WITH_VTE=1 \
        PKG_CONFIG="$check_dir/pkg-config" >"$check_dir/output" 2>&1; then
        echo 'Missing VTE dependency silently accepted' >&2; exit 1
    fi
    grep -q 'WITH_VTE=1 requires' "$check_dir/output"
    if [ "$gtk" = 2 ]; then module=vte; else module=vte-2.91; fi
    test "$(cat "$check_dir/probes")" = "$module"
    rm "$check_dir/probes"
done
if make -s help WITH_GTK2=1 WITH_GTK3=1 >"$check_dir/output" 2>&1; then
    echo 'Conflicting GTK selectors silently accepted' >&2; exit 1
fi
grep -q 'conflict' "$check_dir/output"
printf '%s\n' 'build flags: no disabled VTE probes, actionable missing dependencies, GTK conflicts passed'
