#include <errno.h>
#include <fcntl.h>
#include <keystone/keystone.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__x86_64__)
	#define MACHINE_ARCH KS_ARCH_X86
	#define MACHINE_MODE KS_MODE_64
#elif defined(i386) || defined(__i386__)
	#define MACHINE_ARCH KS_ARCH_X86
	#define MACHINE_MODE KS_MODE_32
#else
	#error architecture not supported
#endif

/* Use AT&T syntax by default */
#define ASM_SYNTAX KS_OPT_SYNTAX_ATT

/* Caller should free returned string after use. Returns NULL on failure. */
static char *
_read_file(char const *path, size_t *fsz)
{
	int         fd;
	char       *ret;
	struct stat st;
	size_t      total;
	ssize_t     n;

	ret = NULL;

	fd = open(path, O_RDONLY);
	if (fd == -1) {
		perror("_read_file(): open()");
		goto cleanup;
	}

	if (fstat(fd, &st) == -1) {
		perror("_read_file(): fstat()");
		goto close_file;
	}

	ret = malloc(st.st_size);
	if (!ret) {
		perror("_read_file(): malloc()");
		goto close_file;
	}

	for (total = 0; total < (size_t)st.st_size; total += (size_t)n) {
		n = read(fd, ret + total, st.st_size - total);
		if (n == 0)
			break; /* EOF */

		if (n < 0) {
			if (errno == EINTR)
				continue;

			perror("_read_file(): read()");
			free(ret);
			ret = NULL;
			goto close_file;
		}
	}

	*fsz = total;

close_file:
	close(fd);
cleanup:
	return ret;
}

/* 'instrs' should be a NULL-terminated string. Caller should free the returned pointer after use.
 * Returns NULL on failure. */
static uint8_t *
_assemble_instrs(char const *instrs, size_t *nb)
{
	ks_engine *ks;
	ks_err     err;
	size_t     cnt;
	uint8_t   *encode, *ret;

	ret = NULL;

	if ((err = ks_open(MACHINE_ARCH, MACHINE_MODE, &ks)) != KS_ERR_OK) {
		fprintf(stderr, "_assemble_instrs(): ks_open(): %s\n", ks_strerror(err));
		goto cleanup;
	}

	if (ks_option(ks, KS_OPT_SYNTAX, ASM_SYNTAX) != KS_ERR_OK) {
		err = ks_errno(ks);
		fprintf(stderr, "_assemble_instrs(): ks_option(): %s\n", ks_strerror(err));
		goto close_ks;
	}

	if (ks_asm(ks, instrs, 0, (unsigned char **)&encode, nb, &cnt) != KS_ERR_OK) {
		err = ks_errno(ks);
		fprintf(stderr, "_assemble_instrs(): ks_asm(): %s\n", ks_strerror(err));
		goto close_ks;
	}

	ret = malloc(*nb);
	if (!ret) {
		perror("_assemble_instrs(): malloc()");
		goto free_encode;
	}

	memcpy(ret, encode, *nb);

free_encode:
	ks_free(encode);
close_ks:
	ks_close(ks);
cleanup:
	return ret;
}

/* Map the necessary memory and write the specified instructions to it. Returns a valid function
 * pointer on success, and NULL on failure. The caller should unmap the function after use.
 */
static void *
_build_asm_fn(uint8_t const *asmb, size_t nb)
{
	void *ret;

	ret = mmap(NULL, nb, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
	if (ret == MAP_FAILED) {
		perror("_build_asm_fn(): mmap()");
		ret = NULL;
		goto cleanup;
	}

	memcpy(ret, asmb, nb);

	if (mprotect(ret, nb, PROT_READ | PROT_EXEC) == -1) {
		perror("_build_asm_fn(): mprotect()");
		goto unmap_func;
	}

	/* This is not necessary on x86. The architecture guarantees instruction cache
	 * coherence. It simply serves a conceptual purpose here. However, this is necessary for
	 * certain architectures that do not guarantee instruction cache coherence, such as the ARM
	 * family. */
	__builtin___clear_cache(ret, (char *)ret + nb);

	goto cleanup;

unmap_func:
	munmap(ret, nb);
	ret = NULL;
cleanup:
	return ret;
}

typedef long int (*asm_fn_t)(void);

int
main(void)
{
	char    *instrs, *tmp;
	uint8_t *asmb;
	size_t   fsz, asmnb;
	int      rc;
	asm_fn_t fn;

	rc     = EXIT_FAILURE;
	instrs = _read_file("asm.s", &fsz);
	if (!instrs)
		goto cleanup;

	tmp = realloc(instrs, fsz + 1);
	if (!tmp) {
		perror("main(): realloc()");
		goto free_instrs;
	}
	instrs      = tmp;
	instrs[fsz] = '\0';

	asmb = _assemble_instrs(instrs, &asmnb);
	if (!asmb || !asmnb)
		goto free_instrs;

	fn = _build_asm_fn(asmb, asmnb);
	if (!fn)
		goto free_asm;

	printf("Function returned: %ld\n", fn());

	rc = EXIT_SUCCESS;

	munmap(fn, asmnb);
free_asm:
	free(asmb);
free_instrs:
	free(instrs);
cleanup:
	return rc;
}
