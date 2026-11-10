#!/bin/bash
# canon-gm2000-macos-driver - installer
# Builds the open driver for Canon PIXMA GM2000 series printers (GM2070 etc.),
# installs it and adds the printer to macOS.
#
# Usage:
#   bash install.sh <printer-IP>                 e.g. bash install.sh 192.168.1.50
#   bash install.sh lpd://<printer-IP>/          use LPD (port 515) instead of port 9100
#   bash install.sh <printer-IP> --paper Letter  default paper size (A4 or Letter, default A4)
#   bash install.sh <printer-IP> --name MyCanon  printer queue name (default Canon_GM2000)
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: bash install.sh <printer-IP or URI> [--paper A4|Letter] [--name QUEUE]

Find your printer's IP address on the printer's network settings page/print-out,
in your router's list of devices, or in Terminal with:
    dns-sd -B _printer._tcp      (press Ctrl+C after a few seconds, then)
    dns-sd -G v4 "<name shown>.local"
EOF
  exit 1
}

TARGET=""; PAPER="A4"; QUEUE="Canon_GM2000"
while [ $# -gt 0 ]; do
  case "$1" in
    --paper) PAPER="${2:-}"; shift 2 ;;
    --name)  QUEUE="${2:-}"; shift 2 ;;
    -h|--help) usage ;;
    *) [ -z "$TARGET" ] && TARGET="$1" || usage; shift ;;
  esac
done
[ -n "$TARGET" ] || usage
case "$PAPER" in A4|Letter) ;; *) echo "--paper must be A4 or Letter"; exit 1 ;; esac
case "$TARGET" in
  *://*) URI="$TARGET" ;;
  *)     URI="socket://$TARGET:9100" ;;
esac

DEST="/Library/Printers/CanonGM2000Open"
FILTERDIR="$DEST/filter"
PPDDEST="/Library/Printers/PPDs/Contents/Resources/CanonGM2000Open.ppd"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD="/tmp/gm2000open-build"   # no spaces allowed: Canon's filter runs its helpers through /bin/sh

say() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
die() { printf '\n\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }

[ "$(uname -s)" = "Darwin" ] || die "This installer is for macOS."
say "macOS $(sw_vers -productVersion) on $(uname -m)"

# 1. Compiler (Xcode Command Line Tools)
if ! xcode-select -p >/dev/null 2>&1 || ! xcrun --find clang >/dev/null 2>&1; then
  say "Apple's Command Line Tools are needed to compile the driver."
  xcode-select --install || true
  die "Finish the Command Line Tools install in the window that opened, then run this script again."
fi
SDK="$(xcrun --show-sdk-path)"
CC="$(xcrun --find clang)"
[ -f "$SDK/usr/include/cups/raster.h" ] || die "CUPS headers not found in the macOS SDK ($SDK)."
[ -d "$SDK/usr/include/libxml2" ] || die "libxml2 headers not found in the macOS SDK ($SDK)."

CUPSIMG=""
if ls "$SDK"/usr/lib/libcupsimage* >/dev/null 2>&1; then CUPSIMG="-lcupsimage"; fi
CFLAGS="-O2 -isysroot $SDK -Wno-deprecated-declarations -Wno-everything"

# 2. Build
say "Compiling the driver"
rm -rf "$BUILD"; mkdir -p "$BUILD"
"$CC" $CFLAGS -DPROG_PATH="\"$FILTERDIR\"" "$HERE"/src/rastertocanonij/*.c \
      -o "$BUILD/rastertocanonij" -lcups
"$CC" $CFLAGS -DPROG_PATH="\"$FILTERDIR\"" -I"$SDK/usr/include/libxml2" "$HERE"/src/tocnpwg/*.c \
      -o "$BUILD/tocnpwg" -lcups $CUPSIMG -lxml2 \
  || "$CC" $CFLAGS -DPROG_PATH="\"$FILTERDIR\"" -I"$SDK/usr/include/libxml2" "$HERE"/src/tocnpwg/*.c \
      -o "$BUILD/tocnpwg" -lcups -lxml2
"$CC" $CFLAGS "$HERE"/src/tocanonij/tocanonij.c -o "$BUILD/tocanonij"
file "$BUILD"/rastertocanonij "$BUILD"/tocnpwg "$BUILD"/tocanonij | sed 's|.*/||'

# 3. Dry run: render a test page through the whole chain without the printer
say "Testing the driver on this Mac (no printing yet)"
TESTPPD="$BUILD/test.ppd"
sed "s|$FILTERDIR/rastertocanonij|$BUILD/rastertocanonij|" "$HERE/ppd/CanonGM2000Open.ppd" > "$TESTPPD"
printf 'Canon GM2000 series - open macOS driver test page\n\nIf you can read this, printing works.\n' > "$BUILD/test.txt"
if /usr/sbin/cupsfilter -p "$TESTPPD" -m printer/canon -e "$BUILD/test.txt" > "$BUILD/test.out" 2> "$BUILD/test.log" \
   && head -c 5 "$BUILD/test.out" | grep -q '<?xml' && grep -q 'EndJob' "$BUILD/test.out"; then
  echo "OK: produced $(wc -c < "$BUILD/test.out" | tr -d ' ') bytes of printer data."
else
  tail -20 "$BUILD/test.log" || true
  die "Dry run failed. Please open a GitHub issue and attach $BUILD/test.log"
fi

# 4. Install (needs your Mac password)
say "Installing to $DEST (you'll be asked for your Mac password)"
sudo mkdir -p "$FILTERDIR" "$(dirname "$PPDDEST")"
sudo install -o root -g wheel -m 755 "$BUILD/rastertocanonij" "$BUILD/tocnpwg" "$BUILD/tocanonij" "$FILTERDIR/"
sudo install -o root -g wheel -m 644 "$HERE/LICENSE" "$HERE/README.md" "$DEST/"
sudo install -o root -g wheel -m 644 "$HERE/ppd/CanonGM2000Open.ppd" "$PPDDEST"
sudo chown -R root:wheel "$DEST"; sudo chmod 755 "$DEST" "$FILTERDIR"

# 5. Add the printer
say "Adding printer '$QUEUE' at $URI (default paper $PAPER)"
sudo lpadmin -x "$QUEUE" 2>/dev/null || true
sudo lpadmin -p "$QUEUE" -E -v "$URI" -P "$PPDDEST" \
     -D "Canon GM2000 series" \
     -o PageSize="$PAPER" -o MediaType=plain -o CNGrayscale=True -o Duplex=None \
     -o printer-is-shared=false
sudo cupsenable "$QUEUE"; sudo cupsaccept "$QUEUE"
lpstat -p "$QUEUE"

say "Done. Print a test page now? [y/N]"
read -r ans || ans=n
if [[ "$ans" =~ ^[Yy] ]]; then
  lp -d "$QUEUE" -t "GM2000 test" "$BUILD/test.txt"
  echo "Sent. If nothing prints within a minute, try LPD:  bash install.sh lpd://${TARGET#*://}/"
fi
