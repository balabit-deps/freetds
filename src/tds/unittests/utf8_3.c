/* FreeTDS - Library of routines accessing Sybase and Microsoft databases
 * Copyright (C) 1998, 1999, 2000, 2001, 2002, 2003, 2004  Brian Bruns
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#include "common.h"

#include <ctype.h>
#include <assert.h>

static char software_version[] = "$Id: utf8_3.c,v 1.1 2005-01-13 15:01:44 freddy77 Exp $";
static void *no_unused_var_warn[] = { software_version, no_unused_var_warn };

static TDSSOCKET *tds;

static int max_len = 0;

static char *
to_utf8(const char *src, char *dest)
{
	unsigned char *p = (unsigned char *) dest;
	int len = 0;

	for (; *src;) {
		if (src[0] == '&' && src[1] == '#') {
			const char *end = strchr(src, ';');
			char tmp[16];
			int radix = 10;
			int n;

			assert(end);
			src += 2;
			if (toupper(*src) == 'X') {
				radix = 16;
				++src;
			}
			memcpy(tmp, src, end - src);
			tmp[end - src] = 0;
			n = strtol(tmp, NULL, radix);
			assert(n > 0 && n < 0x10000);
			if (n >= 0x1000) {
				*p++ = 0xe0 | (n >> 12);
				*p++ = 0x80 | ((n >> 6) & 0x3f);
				*p++ = 0x80 | (n & 0x3f);
			} else if (n >= 0x80) {
				*p++ = 0xc0 | (n >> 6);
				*p++ = 0x80 | (n & 0x3f);
			} else {
				*p++ = (unsigned char) n;
			}
			src = end + 1;
		} else {
			*p++ = *src++;
		}
		++len;
	}
	if (len > max_len)
		max_len = len;
	*p = 0;
	return dest;
}

static void
query(const char *sql)
{
	if (run_query(tds, sql) != TDS_SUCCEED) {
		fprintf(stderr, "error executing query: %s\n", sql);
		exit(1);
	}
}

static void
test(const char *buf)
{
	char query[1024];
	char tmp[129 * 3];
	int i;
	const char **s;
	int rc;
	TDS_INT result_type;
	TDS_INT row_type;
	TDS_INT compute_id;
	int done_flags;

	to_utf8(buf, tmp);
	sprintf(query, "SELECT 1 AS [%s]", tmp);

	/* do a select and check all results */
	rc = tds_submit_query(tds, query);
	if (rc != TDS_SUCCEED) {
		fprintf(stderr, "tds_submit_query() failed\n");
		exit(1);
	}

	if (tds_process_result_tokens(tds, &result_type, NULL) != TDS_SUCCEED) {
		fprintf(stderr, "tds_process_result_tokens() failed\n");
		exit(1);
	}

	if (result_type != TDS_ROWFMT_RESULT) {
		fprintf(stderr, "expected row fmt() failed\n");
		exit(1);
	}

	if (tds_process_result_tokens(tds, &result_type, NULL) != TDS_SUCCEED) {
		fprintf(stderr, "tds_process_result_tokens() failed\n");
		exit(1);
	}

	if (result_type != TDS_ROW_RESULT) {
		fprintf(stderr, "expected row result() failed\n");
		exit(1);
	}

	i = 0;
	while ((rc = tds_process_row_tokens(tds, &row_type, &compute_id)) == TDS_SUCCEED) {

		TDSCOLUMN *curcol = tds->current_results->columns[0];

		if (strlen(tmp) != curcol->column_namelen || strncmp(tmp, curcol->column_name, curcol->column_namelen) != 0) {
			int l = curcol->column_cur_size;

			if (l > 200)
				l = 200;
			strncpy(query, curcol->column_name, l);
			query[l] = 0;
			fprintf(stderr, "Wrong result Got: '%s' len %d\n Expected: '%s' len %u\n", query,
				curcol->column_namelen, tmp, (unsigned int) strlen(tmp));
			exit(1);
		}
		++i;
	}

	if (rc != TDS_NO_MORE_ROWS) {
		fprintf(stderr, "tds_process_row_tokens() unexpected return\n");
		exit(1);
	}

	while ((rc = tds_process_result_tokens(tds, &result_type, &done_flags)) == TDS_SUCCEED) {
		switch (result_type) {
		case TDS_NO_MORE_RESULTS:
			return;

		case TDS_DONE_RESULT:
		case TDS_DONEPROC_RESULT:
		case TDS_DONEINPROC_RESULT:
			if (!(done_flags & TDS_DONE_ERROR))
				break;

		default:
			fprintf(stderr, "tds_process_result_tokens() unexpected result_type\n");
			exit(1);
			break;
		}
	}
}

int
main(int argc, char **argv)
{
	TDSLOGIN *login;
	int ret;
	int verbose = 0;

	/* use UTF-8 as our coding */
	strcpy(CHARSET, "UTF-8");

	ret = try_tds_login(&login, &tds, __FILE__, verbose);
	if (ret != TDS_SUCCEED) {
		fprintf(stderr, "try_tds_login() failed\n");
		return 1;
	}

	if (IS_TDS7_PLUS(tds)) {
		char type[32];
		char buf[129 * 8];
		int i;

		/* build a string of length 128 */
		strcpy(buf, "");
		for (i = 1; i <= 128; ++i) {
			sprintf(strchr(buf, 0), "&#x%04x;", 0x4000 + i);
		}

		/* do all test */
		for (i = 1;;) {
			fprintf(stderr, "Testing len %d\n", i);
			test(buf + 8 * (128 - i));
			if (i == 128)
				break;
			++i;
			if (i > 12)
				i += 3;
			if (i >= 128)
				i = 128;
		}
	}

	try_tds_logout(login, tds, verbose);
	return 0;
}
