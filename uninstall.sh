#!/bin/bash
# canon-gm2000-macos-driver - removes the driver and the printer queue(s) it created.
# Usage: bash uninstall.sh [QUEUE]     (default: Canon_GM2000)
set -u
QUEUE="${1:-Canon_GM2000}"
echo "Removing printer queue and driver files (you may be asked for your Mac password)..."
for q in "$QUEUE" Canon_GM2070; do
  sudo lpadmin -x "$q" 2>/dev/null && echo "Removed printer $q"
done
sudo rm -rf /Library/Printers/CanonGM2000Open
sudo rm -f /Library/Printers/PPDs/Contents/Resources/CanonGM2000Open.ppd
rm -rf /tmp/gm2000open-build
echo "Done."
