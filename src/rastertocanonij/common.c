/*
 * MODIFIED 2026-11-10 for the macOS port (canon-gm2000-macos-driver):
 *   GetExecProgPath(): use _NSGetExecutablePath() on macOS (no /proc).
 * Changes are marked "macOS port". Original: Canon cnijfilter2 5.90 (GPLv2).
 */
/*
 *  CUPS add-on module for Canon Inkjet Printer.
 *  Copyright CANON INC. 2001-2019
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307, USA.
 */

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#define BUF_SIZE  1024

/*-------------------------------------------------------------------------------
 * GetExecProgPath
 * Get execute program path
-------------------------------------------------------------------------------*/
char *GetExecProgPath( void )
{
    static char buf[BUF_SIZE]={};
    char path[BUF_SIZE];
	int len = 0;
	// char *result = NULL;
    ssize_t read_len = 0;

#ifdef __APPLE__
    /* macOS port: no /proc, ask dyld for our own path */
    {
        uint32_t sz = sizeof(path);
        char real[BUF_SIZE];
        read_len = -1;
        if ( _NSGetExecutablePath( path, &sz ) == 0 && realpath( path, real ) != NULL ) {
            strncpy( buf, real, sizeof(buf)-1 );
            read_len = (ssize_t)strlen( buf );
        }
    }
#else
    snprintf( path, BUF_SIZE, "/proc/%d/exe", getpid() );
    read_len = readlink( path, buf, sizeof(buf)-1 );
#endif

	if( read_len < 0 ){
		goto onErr;
	}

	len = strlen(buf);
	while( len > 0 ){
		if ( '/' == buf[len] ){
			buf[len + 1] = '\0';
			break;
		}
		else {
			len--;
		}
	}

	if ( len != 0 ){
		// result = buf;
	}
	else {
		// result = NULL;
	}

onErr:
    return buf;
}
