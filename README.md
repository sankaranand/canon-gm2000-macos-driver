# Canon PIXMA GM2000 series driver for macOS (GM2070)

An open-source macOS printer driver for the **Canon PIXMA GM2070** and other **GM2000 series** ink-tank printers. Canon publishes drivers for these printers only for Windows and Linux. Without a driver, macOS can see the printer on the network but can't print to it. AirPrint isn't offered, and "Generic PostScript Printer" prints nothing.

This project builds a native driver on your Mac from Canon's own open-source Linux filters. It adds a small open replacement for the one part of Canon's Linux driver that is closed and Linux-only.

- ✅ Works on **Apple Silicon and Intel Macs**. Tested on a MacBook Air M2 with macOS 27.2.
- ✅ Prints over the network on **raw port 9100**, or **LPD** (port 515) as a fallback.
- ✅ Supports every paper size and media type the printer supports, two-sided printing, and grayscale or colour (the CL-741 cartridge is optional). Single-sided A4 grayscale has been tested on paper; the other settings produce output identical to Canon's driver, but haven't all been tried on a real printer yet.
- ✅ Printer output is **byte-for-byte identical to Canon's Linux driver** for every setting ([see verification](#verification)).
- ❌ Ink levels, status messages, and head cleaning or nozzle checks from the Mac aren't supported. Use the printer's own buttons for maintenance.

> Not affiliated with or endorsed by Canon Inc. "Canon" and "PIXMA" are trademarks of Canon Inc. Use at your own risk.

## Supported printers

| Model | Status |
|---|---|
| PIXMA GM2070 | ✅ Tested |
| Other GM2000 series models sold under regional names (e.g. GM2020, GM2040, GM2050) | Should work, since they use the same Canon PPD (`canongm2000.ppd`), but untested. Please report back. |
| GM2080, GM4000 series | ❌ Not covered. They use different PPDs. |

## Install

You need a Mac on the same network as the printer, and about 10 minutes.

1. **Find the printer's IP address.** Print the network settings page from the printer, check your router's list of devices, or run `dns-sd -B _printer._tcp` in Terminal. Tip: give the printer a fixed IP (a "DHCP reservation") in your router, so the address never changes.
2. **Download this project.** Click **Code → Download ZIP** above and unzip it in Downloads.
3. **Open Terminal and run:**
   ```bash
   cd ~/Downloads/canon-gm2000-macos-driver-main
   bash install.sh 192.168.1.50        # ← your printer's IP
   ```
4. If macOS asks to install the **Command Line Tools** (Apple's free compiler), accept, wait for it to finish, then run the command again.
5. Enter your Mac password when asked. Type **y** to print a test page.

The printer then appears as **Canon GM2000 series** in every app's Print dialog.

Options:
```bash
bash install.sh 192.168.1.50 --paper Letter   # default paper size Letter instead of A4
bash install.sh 192.168.1.50 --name Office_Canon
bash install.sh lpd://192.168.1.50/           # use LPD instead of port 9100
```

The installer compiles the driver, **tests it on your Mac before installing anything**, copies it to `/Library/Printers/CanonGM2000Open`, and adds the printer.

## Troubleshooting

**Nothing prints after the test page is sent.** Try LPD instead of port 9100: `bash install.sh lpd://<printer-IP>/`.

**A page spills onto a second sheet and the orange light flashes slowly.** The document's paper size doesn't match the paper in the printer. This usually happens with a Letter-size document printed on A4 paper, which is common with Word and with files imported from Google Docs. Set the document to the paper you load:
- **Word:** File → Page Setup → Paper Size
- **Pages:** Document (toolbar) → Printer & Paper Size

Also set **System Settings → Printers & Scanners → Default paper size**. Press the orange-lit button on the printer to finish or cancel the stuck page.

**Gap at the top of the page.** Same cause as above: a paper size mismatch.

**Printing stopped after the printer's IP changed.** Run the installer again with the new IP. A DHCP reservation in your router prevents this.

**The dry run fails during install.** Open an issue and attach `/tmp/gm2000open-build/test.log`.

**"lpadmin: Printer drivers are deprecated…"** This is a standard macOS warning for every driver of this kind. Printing works today, but a future macOS release may remove driver support entirely.

## Uninstall

```bash
bash uninstall.sh
```

## How it works

Canon's Linux driver (`cnijfilter2` 5.90) converts each page in two stages:

```
macOS print system ──► rastertocanonij ──► tocnpwg ──► tocanonij ──► printer (port 9100)
                       (open, GPLv2)       (open)      CLOSED in Canon's driver:
                                                       libcnbpcnclapicom2.so (Linux-only binary)
```

- `rastertocanonij` and `tocnpwg` are open source. They turn each page into **PWG Raster**, a standard image format. These are compiled for macOS with four small changes, each marked `macOS port`.
- Canon's `tocanonij` calls a **closed, Linux-only library** that wraps the pages in Canon's job commands. That library can't run on a Mac.
- This project replaces that step with [`src/tocanonij/tocanonij.c`](src/tocanonij/tocanonij.c), about 200 lines of plain C. It writes the same job commands, which turn out to be plain XML ("IVEC" commands such as `StartJob`, `SetConfiguration`, `SendData` and `EndJob`).

The full write-up, including how the protocol was worked out and what each command contains, is in **[docs/HOW-IT-WORKS.md](docs/HOW-IT-WORKS.md)**.

## Verification

[`tools/verify-against-canon.sh`](tools/verify-against-canon.sh) runs on x86-64 Linux with Canon's driver source tarball. It builds both Canon's original chain (with the closed library) and this project's open chain. It then prints the same multi-page test document through both with every paper size, media type, duplex mode and colour mode, and checks that the printer data is identical:

```
$ tools/verify-against-canon.sh cnijfilter2-source-5.90-1.tar.gz
  MATCH  PageSize=A4
  MATCH  PageSize=A4 Duplex=DuplexNoTumble
  ...
==> All outputs identical to Canon's driver.
```

## Repository layout

```
install.sh / uninstall.sh   build + install / remove (macOS)
ppd/CanonGM2000Open.ppd     Canon's GM2000 PPD, pointed at the installed filter
src/rastertocanonij/        Canon open-source filter (GPLv2), macOS-patched
src/tocnpwg/                Canon open-source filter (GPLv2), macOS-patched
src/tocanonij/tocanonij.c   new open replacement for Canon's closed command library
tools/verify-against-canon.sh
docs/HOW-IT-WORKS.md
```

## Contributing

Reports from other GM2000 series models are very welcome. Open an issue with your model, macOS version and whether printing works. Adding another Canon model that uses the same `cnijfilter2` architecture mostly means adding its PPD and checking its option names with the verification script.

## License

GPL v2. See [LICENSE](LICENSE). The files under `src/rastertocanonij/`, `src/tocnpwg/` and `ppd/` come from Canon's `cnijfilter2` 5.90 source package, which Canon releases under GPLv2 ([Canon's notice](docs/CANON-LICENSE-cnijfilter-5.90EN.txt), Appendix 2). Modified files carry a dated modification notice. No Canon proprietary binaries are included or required.
