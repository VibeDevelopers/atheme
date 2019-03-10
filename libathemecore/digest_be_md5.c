/* Copyright (C) 1999 Aladdin Enterprises.  All rights reserved.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * L. Peter Deutsch
 * ghost@aladdin.com
 */

/* Independent implementation of MD5 (RFC 1321).
 *
 * This code implements the MD5 Algorithm defined in RFC 1321.
 * It is derived directly from the text of the RFC and not from the
 * reference implementation.
 *
 * The original and principal author of md5.c is L. Peter Deutsch
 * <ghost@aladdin.com>.  Other authors are noted in the change history
 * that follows (in reverse chronological order):
 *
 * 1999-11-04 lpd Edited comments slightly for automatic TOC extraction.
 * 1999-10-18 lpd Fixed typo in header comment (ansi2knr rather than md5).
 * 1999-05-03 lpd Original version.
 */

/* Heavily modified by Aaron M. D. Jones <aaronmdjones@gmail.com> (2018)
 * for general code cleanup and conformance to new algorithm-agnostic
 * digest API for Atheme IRC Services <https://github.com/atheme/atheme/>.
 *
 * MD5 backend for Atheme IRC Services.
 */

#ifndef ATHEME_LAC_DIGEST_FE_INTERNAL_C
#  error "Do not compile me directly; compile digest_frontend.c instead"
#endif /* !ATHEME_LAC_DIGEST_FE_INTERNAL_C */

#define MD5_ROUND1(a, b, c, d, k, r, i)                                 \
    do {                                                                \
        t = s[a] + ((s[b] & s[c]) | (~(s[b]) & s[d])) + x[k] + T[i];    \
        s[a] = s[b] + ((t << r) | (t >> (0x20U - r)));                  \
    } while (0)

#define MD5_ROUND2(a, b, c, d, k, r, i)                                 \
    do {                                                                \
        t = s[a] + ((s[b] & s[d]) | (s[c] & ~(s[d]))) + x[k] + T[i];    \
        s[a] = s[b] + ((t << r) | (t >> (0x20U - r)));                  \
    } while (0)

#define MD5_ROUND3(a, b, c, d, k, r, i)                                 \
    do {                                                                \
        t = s[a] + (s[b] ^ s[c] ^ s[d]) + x[k] + T[i];                  \
        s[a] = s[b] + ((t << r) | (t >> (0x20U - r)));                  \
    } while (0)

#define MD5_ROUND4(a, b, c, d, k, r, i)                                 \
    do {                                                                \
        t = s[a] + (s[c] ^ (s[b] | ~(s[d]))) + x[k] + T[i];             \
        s[a] = s[b] + ((t << r) | (t >> (0x20U - r)));                  \
    } while (0)

static void
process_words_md5(struct digest_context_md5 *const ctx, const unsigned char *data)
{
	static const uint32_t T[] = {

		UINT32_C(0xD76AA478), UINT32_C(0xE8C7B756), UINT32_C(0x242070DB), UINT32_C(0xC1BDCEEE),
		UINT32_C(0xF57C0FAF), UINT32_C(0x4787C62A), UINT32_C(0xA8304613), UINT32_C(0xFD469501),
		UINT32_C(0x698098D8), UINT32_C(0x8B44F7AF), UINT32_C(0xFFFF5BB1), UINT32_C(0x895CD7BE),
		UINT32_C(0x6B901122), UINT32_C(0xFD987193), UINT32_C(0xA679438E), UINT32_C(0x49B40821),
		UINT32_C(0xF61E2562), UINT32_C(0xC040B340), UINT32_C(0x265E5A51), UINT32_C(0xE9B6C7AA),
		UINT32_C(0xD62F105D), UINT32_C(0x02441453), UINT32_C(0xD8A1E681), UINT32_C(0xE7D3FBC8),
		UINT32_C(0x21E1CDE6), UINT32_C(0xC33707D6), UINT32_C(0xF4D50D87), UINT32_C(0x455A14ED),
		UINT32_C(0xA9E3E905), UINT32_C(0xFCEFA3F8), UINT32_C(0x676F02D9), UINT32_C(0x8D2A4C8A),
		UINT32_C(0xFFFA3942), UINT32_C(0x8771F681), UINT32_C(0x6D9D6122), UINT32_C(0xFDE5380C),
		UINT32_C(0xA4BEEA44), UINT32_C(0x4BDECFA9), UINT32_C(0xF6BB4B60), UINT32_C(0xBEBFBC70),
		UINT32_C(0x289B7EC6), UINT32_C(0xEAA127FA), UINT32_C(0xD4EF3085), UINT32_C(0x04881D05),
		UINT32_C(0xD9D4D039), UINT32_C(0xE6DB99E5), UINT32_C(0x1FA27CF8), UINT32_C(0xC4AC5665),
		UINT32_C(0xF4292244), UINT32_C(0x432AFF97), UINT32_C(0xAB9423A7), UINT32_C(0xFC93A039),
		UINT32_C(0x655B59C3), UINT32_C(0x8F0CCC92), UINT32_C(0xFFEFF47D), UINT32_C(0x85845DD1),
		UINT32_C(0x6FA87E4F), UINT32_C(0xFE2CE6E0), UINT32_C(0xA3014314), UINT32_C(0x4E0811A1),
		UINT32_C(0xF7537E82), UINT32_C(0xBD3AF235), UINT32_C(0x2AD7D2BB), UINT32_C(0xEB86D391),
	};

	const bool digest_is_big_endian = (htonl(UINT32_C(0x11223344)) == UINT32_C(0x11223344));

	uint32_t x[0x10U];
	uint32_t s[0x04U];
	uint32_t t;

	if (! digest_is_big_endian)
		(void) memcpy(x, data, sizeof x);
	else for (size_t i = 0x00U; i < 0x10U; i++)
		x[i] = data[i] + (data[i + 0x01U] << 0x08U) + (data[i + 0x02U] << 0x10U) + (data[i + 0x03U] << 0x18U);

	(void) memcpy(s, ctx->state, sizeof s);

	MD5_ROUND1(0x00U, 0x01U, 0x02U, 0x03U, 0x00U, 0x07U, 0x00U);
	MD5_ROUND1(0x03U, 0x00U, 0x01U, 0x02U, 0x01U, 0x0CU, 0x01U);
	MD5_ROUND1(0x02U, 0x03U, 0x00U, 0x01U, 0x02U, 0x11U, 0x02U);
	MD5_ROUND1(0x01U, 0x02U, 0x03U, 0x00U, 0x03U, 0x16U, 0x03U);
	MD5_ROUND1(0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x07U, 0x04U);
	MD5_ROUND1(0x03U, 0x00U, 0x01U, 0x02U, 0x05U, 0x0CU, 0x05U);
	MD5_ROUND1(0x02U, 0x03U, 0x00U, 0x01U, 0x06U, 0x11U, 0x06U);
	MD5_ROUND1(0x01U, 0x02U, 0x03U, 0x00U, 0x07U, 0x16U, 0x07U);
	MD5_ROUND1(0x00U, 0x01U, 0x02U, 0x03U, 0x08U, 0x07U, 0x08U);
	MD5_ROUND1(0x03U, 0x00U, 0x01U, 0x02U, 0x09U, 0x0CU, 0x09U);
	MD5_ROUND1(0x02U, 0x03U, 0x00U, 0x01U, 0x0AU, 0x11U, 0x0AU);
	MD5_ROUND1(0x01U, 0x02U, 0x03U, 0x00U, 0x0BU, 0x16U, 0x0BU);
	MD5_ROUND1(0x00U, 0x01U, 0x02U, 0x03U, 0x0CU, 0x07U, 0x0CU);
	MD5_ROUND1(0x03U, 0x00U, 0x01U, 0x02U, 0x0DU, 0x0CU, 0x0DU);
	MD5_ROUND1(0x02U, 0x03U, 0x00U, 0x01U, 0x0EU, 0x11U, 0x0EU);
	MD5_ROUND1(0x01U, 0x02U, 0x03U, 0x00U, 0x0FU, 0x16U, 0x0FU);

	MD5_ROUND2(0x00U, 0x01U, 0x02U, 0x03U, 0x01U, 0x05U, 0x10U);
	MD5_ROUND2(0x03U, 0x00U, 0x01U, 0x02U, 0x06U, 0x09U, 0x11U);
	MD5_ROUND2(0x02U, 0x03U, 0x00U, 0x01U, 0x0BU, 0x0EU, 0x12U);
	MD5_ROUND2(0x01U, 0x02U, 0x03U, 0x00U, 0x00U, 0x14U, 0x13U);
	MD5_ROUND2(0x00U, 0x01U, 0x02U, 0x03U, 0x05U, 0x05U, 0x14U);
	MD5_ROUND2(0x03U, 0x00U, 0x01U, 0x02U, 0x0AU, 0x09U, 0x15U);
	MD5_ROUND2(0x02U, 0x03U, 0x00U, 0x01U, 0x0FU, 0x0EU, 0x16U);
	MD5_ROUND2(0x01U, 0x02U, 0x03U, 0x00U, 0x04U, 0x14U, 0x17U);
	MD5_ROUND2(0x00U, 0x01U, 0x02U, 0x03U, 0x09U, 0x05U, 0x18U);
	MD5_ROUND2(0x03U, 0x00U, 0x01U, 0x02U, 0x0EU, 0x09U, 0x19U);
	MD5_ROUND2(0x02U, 0x03U, 0x00U, 0x01U, 0x03U, 0x0EU, 0x1AU);
	MD5_ROUND2(0x01U, 0x02U, 0x03U, 0x00U, 0x08U, 0x14U, 0x1BU);
	MD5_ROUND2(0x00U, 0x01U, 0x02U, 0x03U, 0x0DU, 0x05U, 0x1CU);
	MD5_ROUND2(0x03U, 0x00U, 0x01U, 0x02U, 0x02U, 0x09U, 0x1DU);
	MD5_ROUND2(0x02U, 0x03U, 0x00U, 0x01U, 0x07U, 0x0EU, 0x1EU);
	MD5_ROUND2(0x01U, 0x02U, 0x03U, 0x00U, 0x0CU, 0x14U, 0x1FU);

	MD5_ROUND3(0x00U, 0x01U, 0x02U, 0x03U, 0x05U, 0x04U, 0x20U);
	MD5_ROUND3(0x03U, 0x00U, 0x01U, 0x02U, 0x08U, 0x0BU, 0x21U);
	MD5_ROUND3(0x02U, 0x03U, 0x00U, 0x01U, 0x0BU, 0x10U, 0x22U);
	MD5_ROUND3(0x01U, 0x02U, 0x03U, 0x00U, 0x0EU, 0x17U, 0x23U);
	MD5_ROUND3(0x00U, 0x01U, 0x02U, 0x03U, 0x01U, 0x04U, 0x24U);
	MD5_ROUND3(0x03U, 0x00U, 0x01U, 0x02U, 0x04U, 0x0BU, 0x25U);
	MD5_ROUND3(0x02U, 0x03U, 0x00U, 0x01U, 0x07U, 0x10U, 0x26U);
	MD5_ROUND3(0x01U, 0x02U, 0x03U, 0x00U, 0x0AU, 0x17U, 0x27U);
	MD5_ROUND3(0x00U, 0x01U, 0x02U, 0x03U, 0x0DU, 0x04U, 0x28U);
	MD5_ROUND3(0x03U, 0x00U, 0x01U, 0x02U, 0x00U, 0x0BU, 0x29U);
	MD5_ROUND3(0x02U, 0x03U, 0x00U, 0x01U, 0x03U, 0x10U, 0x2AU);
	MD5_ROUND3(0x01U, 0x02U, 0x03U, 0x00U, 0x06U, 0x17U, 0x2BU);
	MD5_ROUND3(0x00U, 0x01U, 0x02U, 0x03U, 0x09U, 0x04U, 0x2CU);
	MD5_ROUND3(0x03U, 0x00U, 0x01U, 0x02U, 0x0CU, 0x0BU, 0x2DU);
	MD5_ROUND3(0x02U, 0x03U, 0x00U, 0x01U, 0x0FU, 0x10U, 0x2EU);
	MD5_ROUND3(0x01U, 0x02U, 0x03U, 0x00U, 0x02U, 0x17U, 0x2FU);

	MD5_ROUND4(0x00U, 0x01U, 0x02U, 0x03U, 0x00U, 0x06U, 0x30U);
	MD5_ROUND4(0x03U, 0x00U, 0x01U, 0x02U, 0x07U, 0x0AU, 0x31U);
	MD5_ROUND4(0x02U, 0x03U, 0x00U, 0x01U, 0x0EU, 0x0FU, 0x32U);
	MD5_ROUND4(0x01U, 0x02U, 0x03U, 0x00U, 0x05U, 0x15U, 0x33U);
	MD5_ROUND4(0x00U, 0x01U, 0x02U, 0x03U, 0x0CU, 0x06U, 0x34U);
	MD5_ROUND4(0x03U, 0x00U, 0x01U, 0x02U, 0x03U, 0x0AU, 0x35U);
	MD5_ROUND4(0x02U, 0x03U, 0x00U, 0x01U, 0x0AU, 0x0FU, 0x36U);
	MD5_ROUND4(0x01U, 0x02U, 0x03U, 0x00U, 0x01U, 0x15U, 0x37U);
	MD5_ROUND4(0x00U, 0x01U, 0x02U, 0x03U, 0x08U, 0x06U, 0x38U);
	MD5_ROUND4(0x03U, 0x00U, 0x01U, 0x02U, 0x0FU, 0x0AU, 0x39U);
	MD5_ROUND4(0x02U, 0x03U, 0x00U, 0x01U, 0x06U, 0x0FU, 0x3AU);
	MD5_ROUND4(0x01U, 0x02U, 0x03U, 0x00U, 0x0DU, 0x15U, 0x3BU);
	MD5_ROUND4(0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x06U, 0x3CU);
	MD5_ROUND4(0x03U, 0x00U, 0x01U, 0x02U, 0x0BU, 0x0AU, 0x3DU);
	MD5_ROUND4(0x02U, 0x03U, 0x00U, 0x01U, 0x02U, 0x0FU, 0x3EU);
	MD5_ROUND4(0x01U, 0x02U, 0x03U, 0x00U, 0x09U, 0x15U, 0x3FU);

	for (size_t i = 0x00U; i < 0x04U; i++)
		ctx->state[i] += s[i];

	(void) smemzero(x, sizeof x);
	(void) smemzero(s, sizeof s);
	(void) smemzero(&t, sizeof t);
}

static bool ATHEME_FATTR_WUR
digest_init_md5(union digest_state *const restrict state)
{
	struct digest_context_md5 *const ctx = (struct digest_context_md5 *) state;

	if (! ctx)
	{
		(void) slog(LG_ERROR, "%s: called with NULL 'ctx' (BUG)", MOWGLI_FUNC_NAME);
		return false;
	}

	static const uint32_t iv[] = {

		UINT32_C(0x67452301), UINT32_C(0xEFCDAB89), UINT32_C(0x98BADCFE), UINT32_C(0x10325476),
	};

	(void) memset(ctx, 0x00U, sizeof *ctx);
	(void) memcpy(ctx->state, iv, sizeof iv);

	return true;
}

static bool ATHEME_FATTR_WUR
digest_update_md5(union digest_state *const restrict state, const void *const restrict data, const size_t len)
{
	struct digest_context_md5 *const ctx = (struct digest_context_md5 *) state;

	if (! ctx)
	{
		(void) slog(LG_ERROR, "%s: called with NULL 'ctx' (BUG)", MOWGLI_FUNC_NAME);
		return false;
	}

	if (! (data && len))
		return true;

	const size_t off = (size_t) (ctx->count[0x00U] >> 0x03U) & (DIGEST_BKLEN_MD5 - 1);
	const uint32_t nbits = (uint32_t) (len << 0x03U);
	const unsigned char *ptr = data;
	size_t rem = len;

	ctx->count[0x00U] += nbits;
	ctx->count[0x01U] += (len >> 0x1DU);

	if (ctx->count[0x00U] < nbits)
		ctx->count[0x01U]++;

	if (off)
	{
		const size_t amt = ((off + len) > DIGEST_BKLEN_MD5) ? (DIGEST_BKLEN_MD5 - off) : len;

		(void) memcpy(ctx->buf + off, ptr, amt);

		if ((off + amt) < DIGEST_BKLEN_MD5)
			return true;

		(void) process_words_md5(ctx, ctx->buf);

		ptr += amt;
		rem -= amt;
	}

	while (rem >= DIGEST_BKLEN_MD5)
	{
		(void) process_words_md5(ctx, ptr);

		ptr += DIGEST_BKLEN_MD5;
		rem -= DIGEST_BKLEN_MD5;
	}

	if (rem)
		(void) memcpy(ctx->buf, ptr, rem);

	return true;
}

static bool ATHEME_FATTR_WUR
digest_final_md5(union digest_state *const restrict state, void *const restrict out)
{
	struct digest_context_md5 *const ctx = (struct digest_context_md5 *) state;

	if (! ctx)
	{
		(void) slog(LG_ERROR, "%s: called with NULL 'ctx' (BUG)", MOWGLI_FUNC_NAME);
		return false;
	}
	if (! out)
	{
		(void) slog(LG_ERROR, "%s: called with NULL 'out' (BUG)", MOWGLI_FUNC_NAME);
		return false;
	}

	unsigned char data[0x08U];

	for (size_t i = 0x00U; i < sizeof data; i++)
		data[i] = (unsigned char) (ctx->count[i >> 0x02U] >> ((i & 0x03U) << 0x03U));

	static const unsigned char padding[] = {

		0x80U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
		0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
		0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
		0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
		0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
	};

	const size_t padsz = ((DIGEST_BKLEN_MD5 - 0x09U) - (ctx->count[0x00U] >> 0x03U)) & (DIGEST_BKLEN_MD5 - 0x01U);

	if (! digest_update_md5(state, padding, padsz + 0x01U))
		return false;

	if (! digest_update_md5(state, data, sizeof data))
		return false;

	unsigned char *const digest = out;

	for (size_t i = 0x00U; i < DIGEST_MDLEN_MD5; i++)
		digest[i] = (unsigned char) (ctx->state[i >> 0x02U] >> ((i & 0x03U) << 0x03U));

	(void) smemzero(data, sizeof data);
	(void) smemzero(ctx, sizeof *ctx);

	return true;
}
