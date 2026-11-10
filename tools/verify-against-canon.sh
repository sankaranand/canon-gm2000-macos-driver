#!/bin/bash
# Verify the open driver against Canon's own Linux driver (run on x86-64 Linux).
#
# Builds Canon's reference filter chain (which loads Canon's closed
# libcnbpcnclapicom2.so) and this project's open chain, prints the same test
# pages through both, and checks the printer data is byte-for-byte identical
# (apart from the job timestamp).
#
# Needs: gcc, cups (cupsfilter), cups-filters, ghostscript, libcups2-dev,
#        libcupsimage2-dev, libxml2-dev, python3
#   Debian/Ubuntu: sudo apt install gcc cups cups-filters ghostscript \
#                  libcups2-dev libcupsimage2-dev libxml2-dev python3
#
# Usage: tools/verify-against-canon.sh /path/to/cnijfilter2-source-5.90-1.tar.gz
#   (Canon's "IJ Printer Driver for Linux (Source file)" from your printer's support page)
set -euo pipefail
TARBALL="${1:?Usage: $0 cnijfilter2-source-5.90-1.tar.gz}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
mkdir -p "$W/canon" "$W/ref" "$W/open"
tar xzf "$TARBALL" -C "$W/canon"
C="$(echo "$W"/canon/cnijfilter2-source-*)"
PPD="$C/ppd/canongm2000.ppd"

echo "==> Building Canon reference chain"
gcc -O2 -w -DPROG_PATH="\"$W/ref\"" -I/usr/include/libxml2 "$C"/tocnpwg/src/*.c -o "$W/ref/tocnpwg" -lcups -lcupsimage -lxml2
gcc -O2 -w -DPROG_PATH="\"$W/ref\"" -I"$C/tocanonij/include/cncl" -I"$C/tocanonij/src" -I"$C/tocanonij/src/common" \
    "$C"/tocanonij/src/*.c -o "$W/ref/tocanonij" -ldl
gcc -O2 -w -DPROG_PATH="\"$W/ref\"" "$C"/rastertocanonij/src/*.c -o "$W/ref/rastertocanonij" -lcups
cp -a "$C"/com/libs_bin64/* "$W/ref/"

echo "==> Building open chain (no Canon binaries)"
gcc -O2 -w -DPROG_PATH="\"$W/open\"" "$REPO"/src/rastertocanonij/*.c -o "$W/open/rastertocanonij" -lcups
gcc -O2 -w -DPROG_PATH="\"$W/open\"" -I/usr/include/libxml2 "$REPO"/src/tocnpwg/*.c -o "$W/open/tocnpwg" -lcups -lcupsimage -lxml2
gcc -O2 -Wall "$REPO"/src/tocanonij/tocanonij.c -o "$W/open/tocanonij"

cat > "$W/multi.ps" <<'EOF'
%!PS
/Helvetica-Bold findfont 40 scalefont setfont
72 700 moveto (Page 1) show 1 0 0 setrgbcolor 72 400 200 100 rectfill showpage
72 700 moveto (Page 2) show 0 0 1 setrgbcolor 72 400 200 100 rectfill showpage
72 700 moveto (Page 3) show 0.5 setgray 72 400 200 100 rectfill showpage
EOF

fail=0
check() {
  local opts="$1"
  cupsfilter -p "$PPD" -m application/vnd.cups-raster -o "$(echo "$opts" | tr ' ' ',')" "$W/multi.ps" > "$W/in.ras" 2>/dev/null
  PPD="$PPD" LD_LIBRARY_PATH="$W/ref" "$W/ref/rastertocanonij" 1 u t 1 "$opts" < "$W/in.ras" > "$W/a.bin" 2>/dev/null
  env -u LD_LIBRARY_PATH PPD="$PPD" "$W/open/rastertocanonij" 1 u t 1 "$opts" < "$W/in.ras" > "$W/b.bin" 2>/dev/null
  if python3 - "$W/a.bin" "$W/b.bin" <<'EOF'
import re,sys
n=lambda p: re.sub(rb'<ivec:datetime>\d+</ivec:datetime>',b'',open(p,'rb').read())
a,b=n(sys.argv[1]),n(sys.argv[2]); sys.exit(0 if a and a==b else 1)
EOF
  then echo "  MATCH  $opts"; else echo "  DIFF   $opts"; fail=1; fi
}

echo "==> Comparing printer output"
check "PageSize=A4"
check "PageSize=A4 Duplex=DuplexNoTumble"
check "PageSize=A4 Duplex=DuplexTumble"
check "PageSize=A4 CNGrayscale=False"
for s in Letter legal A5 B5 Postcard envelop10p envelopdlp businesscard; do check "PageSize=$s"; done
for m in envelope ijpostcard postcard highres; do check "PageSize=A4 MediaType=$m"; done

[ $fail = 0 ] && echo "==> All outputs identical to Canon's driver." || { echo "==> Differences found."; exit 1; }
