/*
 * Copyright (C) 1998 Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM DISCLAIMS
 * ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL INTERNET SOFTWARE
 * CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

 /* $Id: proforma.h,v 1.3 1999/01/19 06:49:32 marka Exp $ */

#ifndef RDATA_GENERIC_#_#_H
#define RDATA_GENERIC_#_#_H

static dns_result_t
fromtext_#(dns_rdataclass_t class, dns_rdatatype_t type,
	   isc_lex_t *lexer, dns_name_t *origin,
	   isc_boolean_t downcase, isc_buffer_t *target) {

	INSIST(type == #);
	INSIST(class == #);

	return (DNS_R_NOTIMPLEMENTED);
}

static dns_result_t
totext_#(dns_rdata_t *rdata, dns_name_t *origin, isc_buffer_t *target) {

	INSIST(rdata->type == #);
	INSIST(rdata->class == #);

	return (DNS_R_NOTIMPLEMENTED);
}

static dns_result_t
fromwire_#(dns_rdataclass_t class, dns_rdatatype_t type,
	   isc_buffer_t *source, dns_decompress_t *dctx,
	   isc_boolean_t downcase, isc_buffer_t *target) {

	INSIST(type == #);
	INSIST(class == #);

	return (DNS_R_NOTIMPLEMENTED);
}

static dns_result_t
towire_#(dns_rdata_t *rdata, dns_compress_t *cctx, isc_buffer_t *target) {

	INSIST(rdata->type == #);
	INSIST(rdata->class == #);

	return (DNS_R_NOTIMPLEMENTED);
}

static int
compare_#(dns_rdata_t *rdata1, dns_rdata_t *rdata2) {

	INSIST(rdata1->type == rdata2->type);
	INSIST(rdata1->class == rdata2->class);
	INSIST(rdata1->type == #);
	INSIST(rdata1->class == #);

	return (-2);
}

static dns_result_t
fromstruct_#(dns_rdataclass_t class, dns_rdatatype_t type, void *source,
	     isc_buffer_t *target) {

	INSIST(type == #);
	INSIST(class == #);

	return (DNS_R_NOTIMPLEMENTED);
}

static dns_result_t
tostruct_#(dns_rdata_t *rdata, void *target) {

	INSIST(rdata->type == #);
	INSIST(rdata->class == #);

	return (DNS_R_NOTIMPLEMENTED);
}
#endif	/* RDATA_GENERIC_#_#_H */
