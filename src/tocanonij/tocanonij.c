/*
 * tocanonij (open replacement) - for Canon PIXMA GM2000 series (GM2070)
 *
 * Replaces Canon's tocanonij + closed libcnbpcnclapicom2 with plain C so the
 * driver can run on macOS (Apple Silicon). It reads the page stream produced
 * by Canon's open-source "tocnpwg" filter (CNDATA header + PWG raster per
 * page) and wraps it in the IVEC XML job commands the printer expects.
 *
 * The command text and option names were recorded by running Canon's Linux
 * driver 5.90 on Linux and capturing its output for every GM2000 PPD choice.
 *
 * Part of canon-gm2000-macos-driver, 2026. Not affiliated with Canon Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation. It is distributed WITHOUT ANY WARRANTY.
 * See the LICENSE file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <time.h>

/* Must match tocnpwg's cndata_def.h exactly (same compiler, same process ABI). */
#define MAGIC_NUMBER_FOR_CNIJPWG 0x12340001
typedef struct cndata {
	long  magic_num;
	long  image_size;
	short next_page;
	short page_num;
	long  reserve[13];
} CNDATA;

#define JOBID "00000002"
#define XHDR  "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
#define NS_IVEC "xmlns:ivec=\"http://www.canon.com/ns/cmd/2008/07/common/\""
#define NS_VCN  "xmlns:vcn=\"http://www.canon.com/ns/cmd/2008/07/canon/\""
#define PSET  "<ivec:param_set servicetype=\"print\"><ivec:jobID>" JOBID "</ivec:jobID>"
#define TAIL  "</ivec:param_set></ivec:contents></cmd>"

typedef struct { const char *ppd; const char *ivec; } Map;

/* PPD choice -> IVEC value, recorded from Canon's library (GM2000 PPD). */
static const Map sizeMap[] = {
	{ "Letter",       "na_letter_8.5x11in" },
	{ "legal",        "na_legal_8.5x14in" },
	{ "A5",           "iso_a5_148x210mm" },
	{ "A4",           "iso_a4_210x297mm" },
	{ "B5",           "jis_b5_182x257mm" },
	{ "Postcard",     "jpn_hagaki_100x148mm" },
	{ "envelop10p",   "na_number-10_4.125x9.5in" },
	{ "envelopdlp",   "iso_dl_110x220mm" },
	{ "businesscard", "custom_canon_55x91mm" },
	{ NULL, NULL }
};
static const Map mediaMap[] = {
	{ "plain",      "stationery" },
	{ "envelope",   "custom-media-type-canon-18" },
	{ "ijpostcard", "custom-media-type-canon-11" },
	{ "postcard",   "custom-media-type-canon-12" },
	{ "highres",    "custom-media-type-canon-9" },
	{ NULL, NULL }
};

static const char *lookup(const Map *m, const char *key, const char *fallback)
{
	if (key) for (; m->ppd; m++) if (!strcmp(m->ppd, key)) return m->ivec;
	return fallback;
}

static int write_all(const void *buf, size_t len)
{
	const char *p = buf;
	while (len > 0) {
		ssize_t n = write(1, p, len);
		if (n < 0) { if (errno == EINTR) continue; return -1; }
		p += n; len -= (size_t)n;
	}
	return 0;
}

static int put(const char *s) { return write_all(s, strlen(s)); }

static ssize_t read_full(int fd, void *buf, size_t len)
{
	char *p = buf; size_t got = 0;
	while (got < len) {
		ssize_t n = read(fd, p + got, len - got);
		if (n < 0) { if (errno == EINTR) continue; return -1; }
		if (n == 0) break;
		got += (size_t)n;
	}
	return (ssize_t)got;
}

enum { OPT_VERSION = 1, OPT_FILTERPATH, OPT_PAPERSIZE, OPT_MEDIATYPE,
       OPT_COLORMODE, OPT_DUPLEXPRINT, OPT_JOBID, OPT_UUID };

int main(int argc, char *argv[])
{
	/* Same option table as Canon's tocanonij, so rastertocanonij's command
	   line (--media, --duplex prefixes etc.) is parsed identically. */
	struct option long_opt[] = {
		{ "version",     required_argument, NULL, OPT_VERSION },
		{ "filterpath",  required_argument, NULL, OPT_FILTERPATH },
		{ "papersize",   required_argument, NULL, OPT_PAPERSIZE },
		{ "mediatype",   required_argument, NULL, OPT_MEDIATYPE },
		{ "grayscale",   required_argument, NULL, OPT_COLORMODE },
		{ "duplexprint", required_argument, NULL, OPT_DUPLEXPRINT },
		{ "jobid",       required_argument, NULL, OPT_JOBID },
		{ "uuid",        required_argument, NULL, OPT_UUID },
		{ 0, 0, 0, 0 },
	};
	const char *papersize = "A4", *media = "plain", *gray = "True", *duplex = "None";
	char uuid[256] = "";
	char cmd[2048];
	int opt, idx;

	while ((opt = getopt_long(argc, argv, "0:", long_opt, &idx)) != -1) {
		switch (opt) {
		case OPT_PAPERSIZE:   papersize = optarg; break;
		case OPT_MEDIATYPE:   media = optarg; break;
		case OPT_COLORMODE:   gray = optarg; break;
		case OPT_DUPLEXPRINT: duplex = optarg; break;
		case OPT_UUID:        snprintf(uuid, sizeof(uuid), "%s", optarg); break;
		case OPT_JOBID:       if (!uuid[0]) snprintf(uuid, sizeof(uuid), "%s", optarg); break;
		default: break;
		}
	}

	const char *ivSize  = lookup(sizeMap, papersize, NULL);
	const char *ivMedia = lookup(mediaMap, media, "stationery");
	if (!ivSize) {
		fprintf(stderr, "ERROR: [tocanonij] unsupported paper size '%s'\n", papersize);
		return 1;
	}
	/* CNGrayscale True -> monochrome; anything else -> color (uses CL-741). */
	const char *ivColor  = (gray && !strcasecmp(gray, "False")) ? "color" : "monochrome";
	const char *ivDuplex = (duplex && strncmp(duplex, "Duplex", 6) == 0) ? "ON" : "OFF";

	/* StartJob */
	snprintf(cmd, sizeof(cmd),
		XHDR "<cmd " NS_IVEC " " NS_VCN "><ivec:contents><ivec:operation>StartJob</ivec:operation>"
		PSET "<ivec:bidi>0</ivec:bidi><vcn:forcepmdetection>OFF</vcn:forcepmdetection>"
		"<ivec:jobname/><ivec:username/><ivec:computername/>"
		"<ivec:job_description><![CDATA[%s]]></ivec:job_description>"
		"<vcn:host_environment>linux</vcn:host_environment>" TAIL, uuid);
	if (put(cmd)) return 1;

	/* SetJobConfiguration (date/time) */
	{
		char dt[32]; time_t t = time(NULL); struct tm *tm = localtime(&t);
		strftime(dt, sizeof(dt), "%Y%m%d%H%M%S", tm);
		snprintf(cmd, sizeof(cmd),
			"<?xml version=\"1.0\" encoding=\"utf-8\"?><cmd " NS_IVEC "><ivec:contents>"
			"<ivec:operation>SetJobConfiguration</ivec:operation>" PSET
			"<ivec:mismatch_mode>none</ivec:mismatch_mode><ivec:datetime>%s</ivec:datetime>" TAIL, dt);
		if (put(cmd)) return 1;
	}

	/* SetConfiguration */
	snprintf(cmd, sizeof(cmd),
		XHDR "<cmd " NS_IVEC "><ivec:contents><ivec:operation>SetConfiguration</ivec:operation>" PSET
		"<ivec:papersize>%s</ivec:papersize><ivec:papertype>%s</ivec:papertype>"
		"<ivec:borderlessprint>OFF</ivec:borderlessprint>"
		"<ivec:printcolormode>%s</ivec:printcolormode><ivec:duplexprint>%s</ivec:duplexprint>" TAIL,
		ivSize, ivMedia, ivColor, ivDuplex);
	if (put(cmd)) return 1;

	/* Pages */
	size_t bufSize = 256 * 1024;
	char *buf = malloc(bufSize);
	if (!buf) return 1;
	int pages = 0;
	for (;;) {
		CNDATA hd;
		ssize_t n = read_full(0, &hd, sizeof(hd));
		if (n == 0) break;
		if (n != (ssize_t)sizeof(hd) || hd.magic_num != MAGIC_NUMBER_FOR_CNIJPWG || hd.image_size < 0) {
			fprintf(stderr, "ERROR: [tocanonij] bad page header from tocnpwg\n");
			free(buf); return 1;
		}
		snprintf(cmd, sizeof(cmd),
			XHDR "<cmd " NS_IVEC " " NS_VCN "><ivec:contents><ivec:operation>VendorCmd</ivec:operation>" PSET
			"<vcn:ijoperation>SetPageConfiguration</vcn:ijoperation><vcn:nextpage>%s</vcn:nextpage>" TAIL,
			hd.next_page ? "ON" : "OFF");
		if (put(cmd)) { free(buf); return 1; }

		snprintf(cmd, sizeof(cmd),
			XHDR "<cmd " NS_IVEC "><ivec:contents><ivec:operation>SendData</ivec:operation>" PSET
			"<ivec:format>PWGRaster</ivec:format><ivec:datasize>%ld</ivec:datasize>" TAIL,
			hd.image_size);
		if (put(cmd)) { free(buf); return 1; }

		long left = hd.image_size;
		while (left > 0) {
			size_t want = (size_t)left < bufSize ? (size_t)left : bufSize;
			ssize_t r = read_full(0, buf, want);
			if (r <= 0) { fprintf(stderr, "ERROR: [tocanonij] short page data\n"); free(buf); return 1; }
			if (write_all(buf, (size_t)r)) { free(buf); return 1; }
			left -= r;
		}
		pages++;
		fprintf(stderr, "INFO: Sent page %d to printer\n", pages);
	}
	free(buf);

	/* EndJob */
	if (put(XHDR "<cmd " NS_IVEC "><ivec:contents><ivec:operation>EndJob</ivec:operation>" PSET TAIL)) return 1;
	return 0;
}
