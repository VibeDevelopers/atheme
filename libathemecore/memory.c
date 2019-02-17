/*
 * SPDX-License-Identifier: ISC
 * SPDX-URL: https://spdx.org/licenses/ISC.html
 *
 * Copyright (C) 2005-2012 Atheme Project (http://atheme.org/)
 * Copyright (C) 2017-2018 Aaron M. D. Jones <aaronmdjones@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * atheme-services: A collection of minimalist IRC services
 * memory.c: Memory wrappers
 */

#include "atheme.h"

#ifdef HAVE_LIBSODIUM
#  include <sodium/utils.h>
#endif /* HAVE_LIBSODIUM */

#ifndef SIGUSR1
#  define RAISE_EXCEPTION       abort()
#else /* !SIGUSR1 */
#  define RAISE_EXCEPTION       do { raise(SIGUSR1); abort(); } while (0)
#endif /* SIGUSR1 */

#ifdef USE_LIBSODIUM_ALLOCATOR

struct sodium_memblock
{
	mowgli_node_t   node;
	void *          ptr;
	size_t          len;
};

static mowgli_list_t *sodium_memblocks = NULL;

static inline void
make_sodium_memblocks_ro(void)
{
	mowgli_node_t *n;

	if (sodium_mprotect_readonly(sodium_memblocks) != 0)
		abort();

	MOWGLI_ITER_FOREACH(n, sodium_memblocks->head)
	{
		struct sodium_memblock *const mptr = n->data;

		if (sodium_mprotect_readonly(mptr) != 0)
			abort();
	}
}

static inline void
make_sodium_memblocks_rw(void)
{
	mowgli_node_t *n;

	if (sodium_mprotect_readwrite(sodium_memblocks) != 0)
		abort();

	MOWGLI_ITER_FOREACH(n, sodium_memblocks->head)
	{
		struct sodium_memblock *const mptr = n->data;

		if (sodium_mprotect_readwrite(mptr) != 0)
			abort();
	}
}

static void
make_sodium_memblock(void *const restrict ptr, const size_t len)
{
	if (! ptr || ! len)
		/* bug in this code */
		abort();

	if (! sodium_memblocks)
	{
		if (! (sodium_memblocks = sodium_malloc(sizeof *sodium_memblocks)))
			/* no free memory? */
			abort();

		(void) memset(sodium_memblocks, 0x00, sizeof *sodium_memblocks);
	}

	struct sodium_memblock *const mptr = sodium_malloc(sizeof *mptr);

	if (! mptr)
		/* no free memory? */
		abort();

	mptr->ptr = ptr;
	mptr->len = len;

	(void) make_sodium_memblocks_rw();
	(void) mowgli_node_add(mptr, &mptr->node, sodium_memblocks);
	(void) make_sodium_memblocks_ro();
}

static struct sodium_memblock *
find_sodium_memblock(void *const restrict ptr)
{
	if (! ptr)
		/* bug in this code */
		abort();

	if (! sodium_memblocks)
		/* srealloc(ptr!=NULL) or sfree(ptr!=NULL) before any s(c|m|re)alloc() calls */
		abort();

	mowgli_node_t *n;

	MOWGLI_ITER_FOREACH(n, sodium_memblocks->head)
	{
		struct sodium_memblock *const mptr = n->data;

		if (mptr->ptr == ptr)
			return mptr;
	}

	return NULL;
}

static void
free_sodium_memblock(struct sodium_memblock *const restrict mptr)
{
	if (! mptr)
		/* bug in this code */
		abort();

	if (! sodium_memblocks)
		/* srealloc(ptr!=NULL,len==0) or sfree(ptr!=NULL) before any s(c|m|re)alloc() calls */
		abort();

	(void) make_sodium_memblocks_rw();
	(void) mowgli_node_delete(&mptr->node, sodium_memblocks);
	(void) make_sodium_memblocks_ro();

	(void) sodium_free(mptr);
}

#endif /* USE_LIBSODIUM_ALLOCATOR */

void
smemzero(void *const restrict ptr, const size_t len)
{
	if (! (ptr && len))
		return;

#ifdef HAVE_MEMSET_S

	if (memset_s(ptr, len, 0x00, len) != 0)
		RAISE_EXCEPTION;

#else /* HAVE_MEMSET_S */
#  ifdef HAVE_EXPLICIT_BZERO

	(void) explicit_bzero(ptr, len);

#  else /* HAVE_EXPLICIT_BZERO */
#    ifdef HAVE_LIBSODIUM_MEMZERO

	(void) sodium_memzero(ptr, len);

#    else /* HAVE_LIBSODIUM_MEMZERO */

	/* We don't have memset_s(3) [ISO 9899:2011], explicit_bzero(3) [OpenBSD], or sodium_memzero(3) [libsodium].
	 *
	 * Indirect memset(3) through a volatile function pointer should hopefully prevent dead-store elimination
	 * removing the call. This may not work if Atheme IRC Services is built with Link Time Optimisation, because
	 * the compiler may be able to prove (for a given definition of proof) that the pointer always points to
	 * memset(3); LTO lets the compiler analyse every compilation unit, not just this one. Alas, the C standard
	 * only requires the compiler to read the value of the pointer, not make the function call through it; so if
	 * it reads it and determines that it still points to memset(3), it can still decide not to call it. To
	 * hopefully prevent the compiler making assumptions about what it points to, it is not located in this
	 * compilation unit; and this unit (and the unit it is located in) is part of a library, so in theory a
	 * consumer of this library could modify this extern variable to point to anything. Still, the C standard
	 * does not guarantee that this will work, and a sufficiently clever compiler may still remove the smemzero
	 * function calls from modules that use this library if Full LTO is used, because nothing in this program
	 * or any of its modules sets the function pointer to any other value.
	 *
	 * Clang <= 7.0 with/without Thin LTO does not remove any calls; other compilers & situations are untested.
	 */

	(void) volatile_memset(ptr, 0x00, len);

#    endif /* !HAVE_LIBSODIUM_MEMZERO */
#  endif /* !HAVE_EXPLICIT_BZERO */
#endif /* !HAVE_MEMSET_S */
}

/* does free()'s job
 *
 * This is only here to balance out the custom malloc()/calloc()/realloc()
 * below. This will be useful if the allocators below are ever repurposed
 * to do something different (e.g. use a hardened memory alloator library).
 */
void
sfree(void *const restrict ptr)
{
#ifdef USE_LIBSODIUM_ALLOCATOR

	if (! ptr)
		/* free(NULL) is well-defined to do nothing */
		return;

	struct sodium_memblock *const mptr = find_sodium_memblock(ptr);

	if (! mptr)
		/* sfree() on something that s(c|m|re)alloc() didn't provide or double-free */
		abort();

	(void) free_sodium_memblock(mptr);
	(void) sodium_free(ptr);

#else /* USE_LIBSODIUM_ALLOCATOR */

	(void) free(ptr);

#endif /* !USE_LIBSODIUM_ALLOCATOR */
}

/* does calloc()'s job and dies if it fails
 *
 * Note that this function *MUST* RETURN ZERO-INITIALIZED MEMORY
 * Parts of the codebase assume this is so and will malfunction otherwise
 */
void * ATHEME_FATTR_ALLOC_SIZE_PRODUCT(1, 2) ATHEME_FATTR_MALLOC ATHEME_FATTR_RETURNS_NONNULL
scalloc(const size_t num, const size_t len)
{
#ifdef USE_LIBSODIUM_ALLOCATOR

	if (! num || ! len)
		/* calloc(x, y) for (x * y) == 0 should return NULL but that would break us */
		abort();

	if (len >= (size_t) (((size_t) SIZE_MAX) / num))
		/* (num * len) would overflow */
		abort();

	const size_t totalsz = (num * len);

	void *const buf = sodium_malloc(totalsz);

	(void) make_sodium_memblock(buf, totalsz);

	return memset(buf, 0x00, totalsz);

#else /* USE_LIBSODIUM_ALLOCATOR */

	void *const buf = calloc(num, len);

	if (! buf)
		RAISE_EXCEPTION;

	return buf;

#endif /* !USE_LIBSODIUM_ALLOCATOR */
}

/* does malloc()'s job and dies if it fails
 *
 * Note that this function *MUST* RETURN ZERO-INITIALIZED MEMORY
 * Parts of the codebase assume this is so and will malfunction otherwise
 */
void * ATHEME_FATTR_ALLOC_SIZE(1) ATHEME_FATTR_MALLOC ATHEME_FATTR_RETURNS_NONNULL
smalloc(const size_t len)
{
	return scalloc(1, len);
}

/* does realloc()'s job and dies if it fails */
void * ATHEME_FATTR_ALLOC_SIZE(2) ATHEME_FATTR_WUR
srealloc(void *const restrict ptr, const size_t len)
{
#ifdef USE_LIBSODIUM_ALLOCATOR

	if (! ptr)
		/* realloc(NULL, x) == malloc(x) */
		return smalloc(len);

	struct sodium_memblock *const mptr = find_sodium_memblock(ptr);

	if (! mptr)
		abort();

	if (! len)
	{
		/* realloc(ptr, 0) == free(ptr) */

		(void) free_sodium_memblock(mptr);
		(void) sodium_free(ptr);

		return NULL;
	}

	if (len == mptr->len)
		/* realloc() to same size as existing alloc -- do nothing */
		return ptr;

	void *const buf = smalloc(len);

	if (len > mptr->len)
		(void) memcpy(buf, ptr, mptr->len);
	else
		(void) memcpy(buf, ptr, len);

	(void) free_sodium_memblock(mptr);
	(void) sodium_free(ptr);

	return buf;

#else /* USE_LIBSODIUM_ALLOCATOR */

	void *const buf = realloc(ptr, len);

	if (len && ! buf)
		RAISE_EXCEPTION;

	return buf;

#endif /* !USE_LIBSODIUM_ALLOCATOR */
}

/* does strdup()'s job, only with the above memory functions */
char * ATHEME_FATTR_MALLOC
sstrdup(const char *const restrict ptr)
{
	if (! ptr)
		return NULL;

	const size_t len = strlen(ptr);
	char *const buf = smalloc(len + 1);

	if (len)
		(void) memcpy(buf, ptr, len);

	return buf;
}

/* does strndup()'s job, only with the above memory functions */
char * ATHEME_FATTR_MALLOC
sstrndup(const char *const restrict ptr, const size_t maxlen)
{
	if (! ptr)
		return NULL;

	const size_t len = strnlen(ptr, maxlen);
	char *const buf = smalloc(len + 1);

	if (len)
		(void) memcpy(buf, ptr, len);

	return buf;
}

void * ATHEME_FATTR_MALLOC
smemdup(const void *const restrict ptr, const size_t len)
{
	if (! ptr || ! len)
		return NULL;

	void *const buf = smalloc(len);

	return memcpy(buf, ptr, len);
}
