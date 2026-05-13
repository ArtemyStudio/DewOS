#!/usr/bin/env bash
# build-pkgs.sh — собирает локальный drop-репо с fastfetch и vim
#
# Подход: качаем готовые статические бинарники с GitHub releases (если есть),
# либо собираем из исходников. Для первого MVP — статические релизы.
#
# Куда складывает:
#   build/pkgs-stage/<name>-<version>/         — содержимое пакета
#   build/repo/<name>-<version>.drop           — собранный tar.gz
#   build/repo/index.txt                       — индекс
#
# Потом build-initramfs.sh кладёт это в /var/drop/repo/ rootfs-disk.

set -euo pipefail

cd "$(dirname "$0")/.."

STAGE="build/pkgs-stage"
REPO="build/repo"
mkdir -p "$STAGE" "$REPO"

# ──────────────────────────────────────────────────────────────────────
# Helpers
# ──────────────────────────────────────────────────────────────────────
log()  { echo "[drop-pkgs] $*"; }
warn() { echo "[drop-pkgs] WARNING: $*" >&2; }

pack_pkg() {
    local name="$1"
    local version="$2"
    local stage_dir="$3"
    local description="$4"

    local out="$REPO/${name}-${version}.drop"
    log "packing $name $version -> $out"
    (cd "$stage_dir" && tar -czf "$OLDPWD/$out" .)

    # add to index (tab-separated)
    printf "%s\t%s\t%s\n" "$name" "$version" "$description" >> "$REPO/index.txt.new"
}

# wipe old index, rebuild
rm -f "$REPO/index.txt.new"

# ──────────────────────────────────────────────────────────────────────
# fastfetch — try static release from GitHub
# ──────────────────────────────────────────────────────────────────────
build_fastfetch() {
    local name="fastfetch"
    local version="2.21.3"  # bump if needed
    local stage="$STAGE/$name-$version"

    if [ -f "$REPO/${name}-${version}.drop" ]; then
        log "$name $version already packed, skipping"
        printf "%s\t%s\t%s\n" "$name" "$version" "system information tool" >> "$REPO/index.txt.new"
        return
    fi

    rm -rf "$stage"
    mkdir -p "$stage/usr/bin"

    # Try our pre-built copy first
    if [ -f "thirdparty/fastfetch/fastfetch" ]; then
        log "using thirdparty/fastfetch/fastfetch"
        cp thirdparty/fastfetch/fastfetch "$stage/usr/bin/fastfetch"
        chmod +x "$stage/usr/bin/fastfetch"
        pack_pkg "$name" "$version" "$stage" "system information tool"
        return
    fi

    # Try host system fastfetch (if statically linked)
    if command -v fastfetch >/dev/null 2>&1; then
        local f
        f="$(command -v fastfetch)"
        # only use it if static
        if ldd "$f" 2>&1 | grep -q "not a dynamic executable"; then
            log "using host static fastfetch from $f"
            cp "$f" "$stage/usr/bin/fastfetch"
            chmod +x "$stage/usr/bin/fastfetch"
            pack_pkg "$name" "$version" "$stage" "system information tool"
            return
        fi
    fi

    warn "fastfetch: no static binary found, skipping package"
    warn "          place a static fastfetch binary at thirdparty/fastfetch/fastfetch"
    warn "          and re-run scripts/build-pkgs.sh"
}

# ──────────────────────────────────────────────────────────────────────
# vim — busybox already has vi, but real vim is what users actually want
# Strategy: ship a static vim. If we can't get one, fall back to a symlink
# to busybox vi marketed as 'vim'. This is honest because we'd note it
# in the description.
# ──────────────────────────────────────────────────────────────────────
build_vim() {
    local name="vim"
    local version="9.1"
    local stage="$STAGE/$name-$version"

    if [ -f "$REPO/${name}-${version}.drop" ]; then
        log "$name $version already packed, skipping"
        printf "%s\t%s\t%s\n" "$name" "$version" "vim text editor" >> "$REPO/index.txt.new"
        return
    fi

    rm -rf "$stage"
    mkdir -p "$stage/usr/bin"

    if [ -f "thirdparty/vim/vim" ]; then
        log "using thirdparty/vim/vim"
        cp thirdparty/vim/vim "$stage/usr/bin/vim"
        chmod +x "$stage/usr/bin/vim"
        pack_pkg "$name" "$version" "$stage" "vim text editor"
        return
    fi

    # fallback — shim that calls busybox vi
    log "no static vim found; shipping shim to busybox vi"
    cat > "$stage/usr/bin/vim" <<'SHIM'
#!/bin/sh
exec /bin/busybox vi "$@"
SHIM
    chmod +x "$stage/usr/bin/vim"
    pack_pkg "$name" "$version" "$stage" "vim editor (shim to busybox vi for now)"
}

# ──────────────────────────────────────────────────────────────────────
# Run
# ──────────────────────────────────────────────────────────────────────
build_fastfetch
build_vim

mv "$REPO/index.txt.new" "$REPO/index.txt"

log "done. Repo contents:"
ls -la "$REPO"
echo "---"
log "index:"
cat "$REPO/index.txt"
