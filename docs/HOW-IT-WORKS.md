# How the macOS port works

## The problem

The Canon PIXMA GM2000 series (GM2070 and others) has official drivers for Windows and Linux only. On a Mac:

- The printer shows up through Bonjour, but macOS finds no driver for it.
- It doesn't offer AirPrint or IPP: port 631 is closed. Its web page only offers Canon's cloud printing setup.
- macOS's fallback, "Generic PostScript Printer", produces nothing, because the printer doesn't understand PostScript.

A port scan shows **80 (web), 515 (LPD) and 9100 (raw)** open. So macOS can deliver data to the printer with its built-in "HP Jetdirect – Socket" (port 9100) or LPD backends. What's missing is something that converts pages into data the printer understands.

## Canon's Linux driver

Canon's `cnijfilter2-source-5.90-1.tar.gz` is a CUPS driver. Here is how it fits together:

| Component | What it does | License |
|---|---|---|
| `canongm2000.ppd` | Describes the printer to CUPS: sizes, media, options | GPLv2 |
| `rastertocanonij` | CUPS filter. Reads the job options from the PPD and starts the two stages below. | GPLv2 |
| `tocnpwg` | Converts each CUPS raster page into PWG Raster, scaled to the printable area, with a small `CNDATA` header per page | GPLv2 |
| `tocanonij` | Wraps the pages in job commands, using `dlopen()` on `libcnbpcnclapicom2.so` | GPLv2 + exception |
| `libcnbpcnclapicom2.so` | **Closed, Linux x86 binary.** Generates the job commands. | Proprietary |
| `cnijbe2`, `cnijlgmon3`, `libcnnet*.so` | USB/network backend and status monitor | Closed parts |

Everything except the job-command library and the network and status parts is open source. macOS already provides the network delivery itself (socket/LPD backends). That leaves one closed component to replace.

## Finding out what the closed library produces

The closed library was **not** disassembled. Instead, Canon's own Linux driver was run unmodified on x86-64 Linux, and its output was recorded:

1. Canon's open filters were compiled, `libcnbpcnclapicom2.so` was placed next to them, and a test page was rendered with `cupsfilter` using the GM2000 PPD.
2. The data `rastertocanonij` would send to the printer was captured.
3. `tocanonij` was run with every value of every PPD option (9 paper sizes, 5 media types, 3 duplex modes, colour and grayscale), and the resulting commands were recorded.

The stream turned out to be simple. It's a series of **plain XML "IVEC" commands** with raw PWG Raster data between them, and no binary framing at all:

```
StartJob              jobID 00000002, bidi 0, host_environment "linux"
SetJobConfiguration   mismatch_mode none, datetime YYYYMMDDhhmmss
SetConfiguration      papersize, papertype, borderlessprint OFF, printcolormode, duplexprint
┌ for each page:
│ VendorCmd           SetPageConfiguration, nextpage ON/OFF
│ SendData            format PWGRaster, datasize N
│ <N bytes of PWG Raster: "RaS2" + 1796-byte header + PackBits-style rows>
└
EndJob
```

The library maps PPD choices to IVEC names as follows:

| PPD `PageSize` | IVEC `papersize` |
|---|---|
| A4 | `iso_a4_210x297mm` |
| Letter | `na_letter_8.5x11in` |
| legal | `na_legal_8.5x14in` |
| A5 | `iso_a5_148x210mm` |
| B5 | `jis_b5_182x257mm` |
| Postcard | `jpn_hagaki_100x148mm` |
| envelop10p | `na_number-10_4.125x9.5in` |
| envelopdlp | `iso_dl_110x220mm` |
| businesscard | `custom_canon_55x91mm` |

| PPD `MediaType` | IVEC `papertype` |
|---|---|
| plain | `stationery` |
| envelope | `custom-media-type-canon-18` |
| ijpostcard | `custom-media-type-canon-11` |
| postcard | `custom-media-type-canon-12` |
| highres | `custom-media-type-canon-9` |

The other options map like this:
- `CNGrayscale`: True becomes `monochrome`, False becomes `color`.
- `Duplex`: None becomes `OFF`; DuplexNoTumble or DuplexTumble become `ON`. Rotating the back side is handled by the open `tocnpwg`.

## The replacement

`src/tocanonij/tocanonij.c` is a self-contained C program. It has no dependencies and no `dlopen`.

- It parses the same command-line options as Canon's `tocanonij`, so `rastertocanonij` drives it unchanged.
- It writes the XML commands above, using the tables above.
- For each page it reads the `CNDATA` header from `tocnpwg`, writes `SetPageConfiguration` and `SendData`, and copies the PWG Raster through.

## Changes to Canon's open filters for macOS

| File | Change | Why |
|---|---|---|
| `rastertocanonij/common.c` | `GetExecProgPath()` uses `_NSGetExecutablePath()` + `realpath()` | macOS has no `/proc/<pid>/exe`; the filter uses its own location to find its helpers |
| `rastertocanonij/paramlist.c` | `<stdlib.h>` instead of `<malloc.h>` | `<malloc.h>` doesn't exist on macOS |
| `rastertocanonij/getsettings.c` | Accept jobs with no CUPS options | The original aborts if the option string is empty; now it uses PPD defaults |
| `tocnpwg/main.c` | Temp files go in `$TMPDIR` (path buffer enlarged) | macOS runs printer filters in a sandbox that allows writing only to its own temp folder, not `/var/tmp` |

The PPD's `*cupsFilter` line points at the absolute install path, `/Library/Printers/CanonGM2000Open/filter/rastertocanonij`, which is where macOS expects third-party filters. The `cmdtocanonij3` command filter (maintenance commands) is removed, because it depends on Canon's closed code.

## Verification

`tools/verify-against-canon.sh` repeats the comparison automatically. Every combination it tests produces output identical to Canon's driver, except for the `datetime` value.

On a real Mac, the PWG page header produced through macOS's own rendering also matches the Linux reference: same 4800 × 6826 pixels for A4 at 600 dpi, same colour space and fields. It differs only by a pixel or two in the image-box values. The page data decodes to exactly the printable height with no extra bytes.

## Known limitations

- **No status or ink levels.** Canon's Linux driver gets these through closed network libraries (`libcnnet2`, `libcnbpnet30`). Jobs are sent one-way over port 9100 or LPD.
- **Paper size must match the loaded paper.** With `mismatch_mode none`, the printer won't warn about a mismatch. A longer page than the loaded sheet continues on the next sheet after you press the button.
- **macOS driver deprecation.** CUPS warns that PPD-based drivers are deprecated. They work on macOS 27, but a future release may remove them. A Linux print server running Canon's driver and sharing the printer via AirPrint is the long-term fallback.
