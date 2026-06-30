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

/* Returns NULL upon failure. Caller should unmap file after use. */
static void *
_map_file(char const *path, int prot, size_t *fsz)
{
	int	    fd;
	void	   *ret;
	struct stat stat;

	ret = NULL;
	fd  = open(path, O_RDONLY);
	if (fd == -1) {
		perror("_map_file(): open()");
		goto cleanup;
	}

	if (fstat(fd, &stat) == -1) {
		perror("_map_file(): fstat()");
		goto close_file;
	}
	*fsz = (size_t)stat.st_size;

	ret = mmap(NULL, *fsz, prot, MAP_SHARED, fd, 0);
	if (ret == MAP_FAILED) {
		ret = NULL;
		perror("_map_file(): mmap()");
	}

close_file:
	close(fd);
cleanup:
	return ret;
}

/* Caller should free the returned pointer after use. Returns NULL on failure. */
static uint8_t *
_assemble_instrs(char const *instrs, size_t *nb)
{
	ks_engine *ks;
	ks_err	   err;
	size_t	   cnt;
	uint8_t	  *encode, *ret;

	ret = NULL;

	if ((err = ks_open(KS_ARCH_X86, KS_MODE_64, &ks)) != KS_ERR_OK) {
		fprintf(stderr, "_assemble_instrs(): ks_open(): %s\n", ks_strerror(err));
		goto cleanup;
	}

	if (ks_option(ks, KS_OPT_SYNTAX, KS_OPT_SYNTAX_ATT) != KS_ERR_OK) {
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
		perror("_run_asm(): mmap()");
		ret = NULL;
		goto cleanup;
	}

	memcpy(ret, asmb, nb);

	if (mprotect(ret, nb, PROT_READ | PROT_EXEC) == -1) {
		perror("_run_asm(): mprotect()");
		goto unmap_func;
	}

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
	void	*mapped_file;
	uint8_t *asmb;
	size_t	 fsz, asmnb;
	int	 rc;
	asm_fn_t fn;

	rc	    = EXIT_FAILURE;
	mapped_file = _map_file("asm.s", PROT_READ, &fsz);
	if (!mapped_file)
		goto cleanup;

	asmb = _assemble_instrs((char const *)mapped_file, &asmnb);
	if (!asmb)
		goto unmap_file;

	fn = _build_asm_fn(asmb, asmnb);
	if (!fn)
		goto free_asm;

	printf("Function returned: %ld\n", fn());

	rc = EXIT_SUCCESS;

	munmap(fn, asmnb);
free_asm:
	free(asmb);
unmap_file:
	munmap(mapped_file, fsz);
cleanup:
	return rc;
}
