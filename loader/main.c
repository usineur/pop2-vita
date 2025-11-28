/* main.c -- Prince of Persia 2: The Shadow and the Flame .so loader
 *
 * Copyright (C) 2025 Matthieu Milan
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.	See the LICENSE file for details.
 */

#include <vitasdk.h>
#include <kubridge.h>
#include <vitashark.h>
#include <vitaGL.h>
#include <zlib.h>
#include <zip.h>

#include <malloc.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <pthread.h>
#include <wchar.h>
#include <wctype.h>
#include <locale.h>
#include <setjmp.h>

#include <math.h>
#include <math_neon.h>

#include <errno.h>
#include <ctype.h>
#include <setjmp.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#include "main.h"
#include "config.h"
#include "dialog.h"
#include "so_util.h"
#include "splash.h"

#define AL_ALEXT_PROTOTYPES
#include <AL/alext.h>
#include <AL/efx.h>

#include "vorbis/vorbisfile.h"

//#define ENABLE_DEBUG

#ifdef ENABLE_DEBUG
#define dlog sceClibPrintf
#else
#define dlog(...)
#endif

static char data_path[14];

static char fake_vm[0x1000];
static char fake_env[0x1000];

int framecap = 0;

int file_exists(const char *path) {
	SceIoStat stat;
	return sceIoGetstat(path, &stat) >= 0;
}

int _newlib_heap_size_user = 128 * 1024 * 1024;

so_module main_mod;

void *__wrap_memcpy(void *dest, const void *src, size_t n) {
	return sceClibMemcpy(dest, src, n);
}

void *__wrap_memmove(void *dest, const void *src, size_t n) {
	return sceClibMemmove(dest, src, n);
}

int ret4() { return 4; }

void *__wrap_memset(void *s, int c, size_t n) {
	return sceClibMemset(s, c, n);
}

char *getcwd_hook(char *buf, size_t size) {
	strcpy(buf, data_path);
	return buf;
}

int posix_memalign(void **memptr, size_t alignment, size_t size) {
	*memptr = memalign(alignment, size);
	return 0;
}

int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
#ifdef ENABLE_DEBUG
	va_list list;
	static char string[0x8000];

	va_start(list, fmt);
	vsprintf(string, fmt, list);
	va_end(list);

	sceClibPrintf("[LOG] %s: %s\n", tag, string);
#endif
	return 0;
}

int __android_log_write(int prio, const char *tag, const char *fmt, ...) {
#ifdef ENABLE_DEBUG
	va_list list;
	static char string[0x8000];

	va_start(list, fmt);
	vsprintf(string, fmt, list);
	va_end(list);

	sceClibPrintf("[LOGW] %s: %s\n", tag, string);
#endif
	return 0;
}

int __android_log_vprint(int prio, const char *tag, const char *fmt, va_list list) {
#ifdef ENABLE_DEBUG
	static char string[0x8000];

	vsprintf(string, fmt, list);
	va_end(list);

	sceClibPrintf("[LOGV] %s: %s\n", tag, string);
#endif
	return 0;
}

int ret0(void) {
	return 0;
}

int ret1(void) {
	return 1;
}

#define  MUTEX_TYPE_NORMAL	 0x0000
#define  MUTEX_TYPE_RECURSIVE  0x4000
#define  MUTEX_TYPE_ERRORCHECK 0x8000

static void init_static_mutex(pthread_mutex_t **mutex)
{
	pthread_mutex_t *mtxMem = NULL;

	switch ((int)*mutex) {
	case MUTEX_TYPE_NORMAL: {
		pthread_mutex_t initTmpNormal = PTHREAD_MUTEX_INITIALIZER;
		mtxMem = calloc(1, sizeof(pthread_mutex_t));
		sceClibMemcpy(mtxMem, &initTmpNormal, sizeof(pthread_mutex_t));
		*mutex = mtxMem;
		break;
	}
	case MUTEX_TYPE_RECURSIVE: {
		pthread_mutex_t initTmpRec = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;
		mtxMem = calloc(1, sizeof(pthread_mutex_t));
		sceClibMemcpy(mtxMem, &initTmpRec, sizeof(pthread_mutex_t));
		*mutex = mtxMem;
		break;
	}
	case MUTEX_TYPE_ERRORCHECK: {
		pthread_mutex_t initTmpErr = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER;
		mtxMem = calloc(1, sizeof(pthread_mutex_t));
		sceClibMemcpy(mtxMem, &initTmpErr, sizeof(pthread_mutex_t));
		*mutex = mtxMem;
		break;
	}
	default:
		break;
	}
}

static void init_static_cond(pthread_cond_t **cond)
{
	if (*cond == NULL) {
		pthread_cond_t initTmp = PTHREAD_COND_INITIALIZER;
		pthread_cond_t *condMem = calloc(1, sizeof(pthread_cond_t));
		sceClibMemcpy(condMem, &initTmp, sizeof(pthread_cond_t));
		*cond = condMem;
	}
}

int pthread_attr_destroy_soloader(pthread_attr_t **attr)
{
	int ret = pthread_attr_destroy(*attr);
	free(*attr);
	return ret;
}

int pthread_attr_getstack_soloader(const pthread_attr_t **attr,
				   void **stackaddr,
				   size_t *stacksize)
{
	return pthread_attr_getstack(*attr, stackaddr, stacksize);
}

__attribute__((unused)) int pthread_condattr_init_soloader(pthread_condattr_t **attr)
{
	*attr = calloc(1, sizeof(pthread_condattr_t));

	return pthread_condattr_init(*attr);
}

__attribute__((unused)) int pthread_condattr_destroy_soloader(pthread_condattr_t **attr)
{
	int ret = pthread_condattr_destroy(*attr);
	free(*attr);
	return ret;
}

int pthread_cond_init_soloader(pthread_cond_t **cond,
				   const pthread_condattr_t **attr)
{
	*cond = calloc(1, sizeof(pthread_cond_t));

	if (attr != NULL)
		return pthread_cond_init(*cond, *attr);
	else
		return pthread_cond_init(*cond, NULL);
}

int pthread_cond_destroy_soloader(pthread_cond_t **cond)
{
	int ret = pthread_cond_destroy(*cond);
	free(*cond);
	return ret;
}

int pthread_cond_signal_soloader(pthread_cond_t **cond)
{
	init_static_cond(cond);
	return pthread_cond_signal(*cond);
}

int pthread_cond_timedwait_soloader(pthread_cond_t **cond,
					pthread_mutex_t **mutex,
					struct timespec *abstime)
{
	init_static_cond(cond);
	init_static_mutex(mutex);
	return pthread_cond_timedwait(*cond, *mutex, abstime);
}

int pthread_create_soloader(pthread_t **thread,
				pthread_attr_t **attr,
				void *(*start)(void *),
				void *param)
{
	*thread = calloc(1, sizeof(pthread_t));

	if (attr != NULL) {
		pthread_attr_setstacksize(*attr, 512 * 1024);
		return pthread_create(*thread, *attr, start, param);
	} else {
		pthread_attr_t attrr;
		pthread_attr_init(&attrr);
		pthread_attr_setstacksize(&attrr, 512 * 1024);
		return pthread_create(*thread, &attrr, start, param);
	}

}

int pthread_mutexattr_init_soloader(pthread_mutexattr_t **attr)
{
	*attr = calloc(1, sizeof(pthread_mutexattr_t));

	return pthread_mutexattr_init(*attr);
}

int pthread_mutexattr_settype_soloader(pthread_mutexattr_t **attr, int type)
{
	return pthread_mutexattr_settype(*attr, type);
}

int pthread_mutexattr_setpshared_soloader(pthread_mutexattr_t **attr, int pshared)
{
	return pthread_mutexattr_setpshared(*attr, pshared);
}

int pthread_mutexattr_destroy_soloader(pthread_mutexattr_t **attr)
{
	int ret = pthread_mutexattr_destroy(*attr);
	free(*attr);
	return ret;
}

int pthread_mutex_destroy_soloader(pthread_mutex_t **mutex)
{
	int ret = pthread_mutex_destroy(*mutex);
	free(*mutex);
	return ret;
}

int pthread_mutex_init_soloader(pthread_mutex_t **mutex,
				const pthread_mutexattr_t **attr)
{
	*mutex = calloc(1, sizeof(pthread_mutex_t));

	if (attr != NULL)
		return pthread_mutex_init(*mutex, *attr);
	else
		return pthread_mutex_init(*mutex, NULL);
}

int pthread_mutex_lock_soloader(pthread_mutex_t **mutex)
{
	init_static_mutex(mutex);
	return pthread_mutex_lock(*mutex);
}

int pthread_mutex_trylock_soloader(pthread_mutex_t **mutex)
{
	init_static_mutex(mutex);
	return pthread_mutex_trylock(*mutex);
}

int pthread_mutex_unlock_soloader(pthread_mutex_t **mutex)
{
	return pthread_mutex_unlock(*mutex);
}

int pthread_join_soloader(const pthread_t *thread, void **value_ptr)
{
	return pthread_join(*thread, value_ptr);
}

int pthread_cond_wait_soloader(pthread_cond_t **cond, pthread_mutex_t **mutex)
{
	return pthread_cond_wait(*cond, *mutex);
}

int pthread_cond_broadcast_soloader(pthread_cond_t **cond)
{
	return pthread_cond_broadcast(*cond);
}

int pthread_attr_init_soloader(pthread_attr_t **attr)
{
	*attr = calloc(1, sizeof(pthread_attr_t));

	return pthread_attr_init(*attr);
}

int pthread_attr_setdetachstate_soloader(pthread_attr_t **attr, int state)
{
	return pthread_attr_setdetachstate(*attr, !state);
}

int pthread_attr_setstacksize_soloader(pthread_attr_t **attr, size_t stacksize)
{
	return pthread_attr_setstacksize(*attr, stacksize);
}

int pthread_attr_getstacksize_soloader(pthread_attr_t **attr, size_t *stacksize)
{
	return pthread_attr_getstacksize(*attr, stacksize);
}

int pthread_attr_setschedparam_soloader(pthread_attr_t **attr,
					const struct sched_param *param)
{
	return pthread_attr_setschedparam(*attr, param);
}

int pthread_attr_getschedparam_soloader(pthread_attr_t **attr,
					struct sched_param *param)
{
	return pthread_attr_getschedparam(*attr, param);
}

int pthread_attr_setstack_soloader(pthread_attr_t **attr,
				   void *stackaddr,
				   size_t stacksize)
{
	return pthread_attr_setstack(*attr, stackaddr, stacksize);
}

int pthread_setschedparam_soloader(const pthread_t *thread, int policy,
				   const struct sched_param *param)
{
	return pthread_setschedparam(*thread, policy, param);
}

int pthread_getschedparam_soloader(const pthread_t *thread, int *policy,
				   struct sched_param *param)
{
	return pthread_getschedparam(*thread, policy, param);
}

int pthread_detach_soloader(const pthread_t *thread)
{
	return pthread_detach(*thread);
}

int pthread_getattr_np_soloader(pthread_t* thread, pthread_attr_t *attr) {
	fprintf(stderr, "[WARNING!] Not implemented: pthread_getattr_np\n");
	return 0;
}

int pthread_equal_soloader(const pthread_t *t1, const pthread_t *t2)
{
	if (t1 == t2)
		return 1;
	if (!t1 || !t2)
		return 0;
	return pthread_equal(*t1, *t2);
}

#ifndef MAX_TASK_COMM_LEN
#define MAX_TASK_COMM_LEN 16
#endif

int pthread_setname_np_soloader(const pthread_t *thread, const char* thread_name) {
	if (thread == 0 || thread_name == NULL) {
		return EINVAL;
	}
	size_t thread_name_len = strlen(thread_name);
	if (thread_name_len >= MAX_TASK_COMM_LEN) {
		return ERANGE;
	}

	// TODO: Implement the actual name setting if possible
	fprintf(stderr, "PTHR: pthread_setname_np with name %s\n", thread_name);

	return 0;
}

int clock_gettime_hook(int clk_id, struct timespec *t) {
	struct timeval now;
	int rv = gettimeofday(&now, NULL);
	if (rv)
		return rv;
	t->tv_sec = now.tv_sec;
	t->tv_nsec = now.tv_usec * 1000;

	return 0;
}


int GetCurrentThreadId(void) {
	return sceKernelGetThreadId();
}

extern void *__aeabi_uldivmod;
extern void *__aeabi_ldiv0;
extern void *__aeabi_ul2f;
extern void *__aeabi_l2f;
extern void *__aeabi_d2lz;
extern void *__aeabi_l2d;

int GetEnv(void *vm, void **env, int r2) {
	*env = fake_env;
	return 0;
}

void throw_exc(char **str, void *a, int b) {
	dlog("throwing %s\n", *str);
}

FILE *fopen_hook(char *fname, char *mode) {
	FILE *f;
	char real_fname[256];
	dlog("fopen(%s,%s)\n", fname, mode);
	if (strncmp(fname, "ux0:", 4)) {
		sprintf(real_fname, "ux0:data/pop2/%s", fname);
		dlog("fopen(%s,%s) patched\n", real_fname, mode);
		f = fopen(real_fname, mode);
	} else {
		f = fopen(fname, mode);
	}
	return f;
}

int open_hook(const char *fname, int flags, mode_t mode) {
	int f;
	char real_fname[256];
	dlog("open(%s)\n", fname);
	if (strncmp(fname, "ux0:", 4)) {
		sprintf(real_fname, "ux0:data/pop2/%s", fname);
		f = open(real_fname, flags, mode);
	} else {
		f = open(fname, flags, mode);
	}
	return f;
}

extern void *__aeabi_atexit;
extern void *__aeabi_ddiv;
extern void *__aeabi_dmul;
extern void *__aeabi_dadd;
extern void *__aeabi_i2d;
extern void *__aeabi_idiv;
extern void *__aeabi_idivmod;
extern void *__aeabi_ldivmod;
extern void *__aeabi_uidiv;
extern void *__aeabi_uidivmod;
extern void *__aeabi_uldivmod;
extern void *__cxa_atexit;
extern void *__cxa_finalize;
extern void *__cxa_call_unexpected;
extern void *__gnu_unwind_frame;
extern void *__stack_chk_fail;

static int __stack_chk_guard_fake = 0x42424242;

static FILE __sF_fake[0x1000][3];

typedef struct __attribute__((__packed__)) stat64_bionic {
	unsigned long long st_dev;
	unsigned char __pad0[4];
	unsigned long st_ino;
	unsigned int st_mode;
	unsigned int st_nlink;
	unsigned long st_uid;
	unsigned long st_gid;
	unsigned long long st_rdev;
	unsigned char __pad3[4];
	unsigned long st_size;
	unsigned long st_blksize;
	unsigned long st_blocks;
	unsigned long st_atime;
	unsigned long st_atime_nsec;
	unsigned long st_mtime;
	unsigned long st_mtime_nsec;
	unsigned long st_ctime;
	unsigned long st_ctime_nsec;
	unsigned long long __pad4;
} stat64_bionic;

int lstat_hook(const char *pathname, stat64_bionic *statbuf) {
	dlog("lstat(%s)\n", pathname);
	int res;
	struct stat st;
	if (strncmp(pathname, "ux0:", 4)) {
		char fname[256];
		sprintf(fname, "ux0:data/pop2/%s", pathname);
		dlog("lstat(%s) fixed\n", fname);
		res = stat(fname, &st);
	} else {
		res = stat(pathname, &st);
	}
	if (res == 0) {
		if (!statbuf) {
			statbuf = malloc(sizeof(stat64_bionic));
		}
		statbuf->st_dev = st.st_dev;
		statbuf->st_ino = st.st_ino;
		statbuf->st_mode = st.st_mode;
		statbuf->st_nlink = st.st_nlink;
		statbuf->st_uid = st.st_uid;
		statbuf->st_gid = st.st_gid;
		statbuf->st_rdev = st.st_rdev;
		statbuf->st_size = st.st_size;
		statbuf->st_blksize = st.st_blksize;
		statbuf->st_blocks = st.st_blocks;
		statbuf->st_atime = st.st_atime;
		statbuf->st_atime_nsec = 0;
		statbuf->st_mtime = st.st_mtime;
		statbuf->st_mtime_nsec = 0;
		statbuf->st_ctime = st.st_ctime;
		statbuf->st_ctime_nsec = 0;
	}
	return res;
}

int stat_hook(const char *pathname, stat64_bionic *statbuf) {
	dlog("stat(%s)\n", pathname);
	int res;
	struct stat st;
	if (strncmp(pathname, "ux0:", 4)) {
		char fname[256];
		sprintf(fname, "ux0:data/pop2/%s", pathname);
		dlog("stat(%s) fixed\n", fname);
		res = stat(fname, &st);
	} else {
		res = stat(pathname, &st);
	}
	if (res == 0) {
		if (!statbuf) {
			statbuf = malloc(sizeof(stat64_bionic));
		}
		statbuf->st_dev = st.st_dev;
		statbuf->st_ino = st.st_ino;
		statbuf->st_mode = st.st_mode;
		statbuf->st_nlink = st.st_nlink;
		statbuf->st_uid = st.st_uid;
		statbuf->st_gid = st.st_gid;
		statbuf->st_rdev = st.st_rdev;
		statbuf->st_size = st.st_size;
		statbuf->st_blksize = st.st_blksize;
		statbuf->st_blocks = st.st_blocks;
		statbuf->st_atime = st.st_atime;
		statbuf->st_atime_nsec = 0;
		statbuf->st_mtime = st.st_mtime;
		statbuf->st_mtime_nsec = 0;
		statbuf->st_ctime = st.st_ctime;
		statbuf->st_ctime_nsec = 0;
	}
	return res;
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
	return memalign(length, 0x1000);
}

int munmap(void *addr, size_t length) {
	free(addr);
	return 0;
}

int fstat_hook(int fd, void *statbuf) {
	struct stat st;
	int res = fstat(fd, &st);
	if (res == 0)
		*(uint64_t *)(statbuf + 0x30) = st.st_size;
	return res;
}

extern void *__cxa_guard_acquire;
extern void *__cxa_guard_release;

void *sceClibMemclr(void *dst, SceSize len) {
	if (!dst) {
		printf("memclr on NULL\n");
		return NULL;
	}
	return sceClibMemset(dst, 0, len);
}

void *sceClibMemset2(void *dst, SceSize len, int ch) {
	return sceClibMemset(dst, ch, len);
}

void *Android_JNI_GetEnv() {
	return fake_env;
}

void abort_hook() {
	dlog("abort called from %p\n", __builtin_return_address(0));
	uint8_t *p = NULL;
	p[0] = 1;
}

int ret99() {
	return 99;
}

int chdir_hook(const char *path) {
	return 0;
}

#define SCE_ERRNO_MASK 0xFF

#define DT_DIR 4
#define DT_REG 8

struct android_dirent {
	char pad[18];
	unsigned char d_type;
	char d_name[256];
};

typedef struct {
	SceUID uid;
	struct android_dirent dir;
} android_DIR;

int closedir_fake(android_DIR *dirp) {
	if (!dirp || dirp->uid < 0) {
		errno = EBADF;
		return -1;
	}

	int res = sceIoDclose(dirp->uid);
	dirp->uid = -1;

	free(dirp);

	if (res < 0) {
		errno = res & SCE_ERRNO_MASK;
		return -1;
	}

	errno = 0;
	return 0;
}

android_DIR *opendir_fake(const char *dirname) {
	dlog("opendir(%s)\n", dirname);
	SceUID uid;
	if (strncmp(dirname, "ux0:", 4)) {
		char real_fname[256];
		sprintf(real_fname, "ux0:data/pop2/%s", dirname);
		uid = sceIoDopen(real_fname);
	} else {
		uid = sceIoDopen(dirname);
	}

	if (uid < 0) {
		errno = uid & SCE_ERRNO_MASK;
		return NULL;
	}

	android_DIR *dirp = calloc(1, sizeof(android_DIR));

	if (!dirp) {
		sceIoDclose(uid);
		errno = ENOMEM;
		return NULL;
	}

	dirp->uid = uid;

	errno = 0;
	return dirp;
}

struct android_dirent *readdir_fake(android_DIR *dirp) {
	if (!dirp) {
		errno = EBADF;
		return NULL;
	}

	SceIoDirent sce_dir;
	int res = sceIoDread(dirp->uid, &sce_dir);

	if (res < 0) {
		errno = res & SCE_ERRNO_MASK;
		return NULL;
	}

	if (res == 0) {
		errno = 0;
		return NULL;
	}

	dirp->dir.d_type = SCE_S_ISDIR(sce_dir.d_stat.st_mode) ? DT_DIR : DT_REG;
	strcpy(dirp->dir.d_name, sce_dir.d_name);
	return &dirp->dir;
}

size_t __strlen_chk(const char *s, size_t s_len) {
	return strlen(s);
}

uint64_t lseek64(int fd, uint64_t offset, int whence) {
	return lseek(fd, offset, whence);
}

void __assert2(const char *file, int line, const char *func, const char *expr) {
	dlog("assertion failed:\n%s:%d (%s): %s\n", file, line, func, expr);
	sceKernelExitProcess(0);
}

void *dlsym_hook( void *handle, const char *symbol) {
	//dlog("dlsym %s\n", symbol);
	return vglGetProcAddress(symbol);
}

int strerror_r_hook(int errnum, char *buf, size_t buflen) {
	strerror_r(errnum, buf, buflen);
	dlog("Error %d: %s\n",errnum, buf);
	return 0;
}

extern void *__aeabi_ul2d;
extern void *__aeabi_d2ulz;

uint32_t fake_stdout;

int access_hook(const char *pathname, int mode) {
	dlog("access(%s)\n", pathname);
	if (strncmp(pathname, "ux0:", 4)) {
		char real_fname[256];
		sprintf(real_fname, "ux0:data/pop2/%s", pathname);
		return access(real_fname, mode);
	}

	return access(pathname, mode);
}

int mkdir_hook(const char *pathname, int mode) {
	dlog("mkdir(%s)\n", pathname);
	if (strncmp(pathname, "ux0:", 4)) {
		char real_fname[256];
		sprintf(real_fname, "ux0:data/pop2/%s", pathname);
		return mkdir(real_fname, mode);
	}

	return mkdir(pathname, mode);
}

FILE *AAssetManager_open(void *mgr, const char *fname, int mode) {
	char full_fname[256];
	sprintf(full_fname, "ux0:data/pop2/%s", fname);
	dlog("AAssetManager_open %s\n", full_fname);
	return fopen(full_fname, "rb");
}

int AAsset_close(FILE *f) {
	return fclose(f);
}

size_t AAsset_getLength(FILE *f) {
	size_t p = ftell(f);
	fseek(f, 0, SEEK_END);
	size_t res = ftell(f);
	fseek(f, p, SEEK_SET);
	return res;
}

size_t AAsset_read(FILE *f, void *buf, size_t count) {
	return fread(buf, 1, count, f);
}

size_t AAsset_seek(FILE *f, size_t offs, int whence) {
	fseek(f, offs, whence);
	return ftell(f);
}

int rmdir_hook(const char *pathname) {
	dlog("rmdir(%s)\n", pathname);
	if (strncmp(pathname, "ux0:", 4)) {
		char real_fname[256];
		sprintf(real_fname, "ux0:data/pop2/%s", pathname);
		return rmdir(real_fname);
	}

	return rmdir(pathname);
}

int unlink_hook(const char *pathname) {
	dlog("unlink(%s)\n", pathname);
	if (strncmp(pathname, "ux0:", 4)) {
		char real_fname[256];
		sprintf(real_fname, "ux0:data/pop2/%s", pathname);
		return sceIoRemove(real_fname);
	}

	return sceIoRemove(pathname);
}

int remove_hook(const char *pathname) {
	dlog("unlink(%s)\n", pathname);
	if (strncmp(pathname, "ux0:", 4)) {
		char real_fname[256];
		sprintf(real_fname, "ux0:data/pop2/%s", pathname);
		return sceIoRemove(real_fname);
	}

	return sceIoRemove(pathname);
}

DIR *AAssetManager_openDir(void *mgr, const char *fname) {
	dlog("AAssetManager_opendir(%s)\n", fname);
	if (strncmp(fname, "ux0:", 4)) {
		char real_fname[256];
		sprintf(real_fname, "ux0:data/pop2/%s", fname);
		return opendir(real_fname);
	}

	return opendir(fname);
}

const char *AAssetDir_getNextFileName(DIR *assetDir) {
	struct dirent *ent = readdir(assetDir);
	if (ent) {
		return ent->d_name;
	}
	return NULL;
}

void AAssetDir_close(DIR *assetDir) {
	closedir(assetDir);
}

int rename_hook(const char *old_filename, const char *new_filename) {
	dlog("rename %s -> %s\n", old_filename, new_filename);
	char real_old[256], real_new[256];
	if (strncmp(old_filename, "ux0:", 4)) {
		sprintf(real_old, "ux0:data/pop2/%s", old_filename);
	} else {
		strcpy(real_old, old_filename);
	}
	if (strncmp(new_filename, "ux0:", 4)) {
		sprintf(real_new, "ux0:data/pop2/%s", new_filename);
	} else {
		strcpy(real_new, new_filename);
	}
	return sceIoRename(real_old, real_new);
}

int nanosleep_hook(const struct timespec *req, struct timespec *rem) {
	const uint32_t usec = req->tv_sec * 1000 * 1000 + req->tv_nsec / 1000;
	return sceKernelDelayThreadCB(usec);
}

int sem_destroy_soloader(int * uid) {
	if (sceKernelDeleteSema(*uid) < 0)
		return -1;
	return 0;
}

int sem_getvalue_soloader (int * uid, int * sval) {
	SceKernelSemaInfo info;
	info.size = sizeof(SceKernelSemaInfo);

	if (sceKernelGetSemaInfo(*uid, &info) < 0) return -1;
	if (!sval) sval = malloc(sizeof(int32_t));
	*sval = info.currentCount;
	return 0;
}

int sem_init_soloader (int * uid, int pshared, unsigned int value) {
	*uid = sceKernelCreateSema("sema", 0, (int) value, 0x7fffffff, NULL);
	if (*uid < 0)
		return -1;
	return 0;
}

int sem_post_soloader (int * uid) {
	if (sceKernelSignalSema(*uid, 1) < 0)
		return -1;
	return 0;
}

uint64_t current_timestamp_ms() {
	struct timeval te;
	gettimeofday(&te, NULL);
	return (te.tv_sec*1000LL + te.tv_usec/1000);
}

int sem_timedwait_soloader (int * uid, const struct timespec * abstime) {
	uint timeout = 1000;
	if (sceKernelWaitSema(*uid, 1, &timeout) >= 0)
		return 0;
	if (!abstime) return -1;
	long long now = (long long) current_timestamp_ms() * 1000; // us
	long long _timeout = abstime->tv_sec * 1000 * 1000 + abstime->tv_nsec / 1000; // us
	if (_timeout-now >= 0) return -1;
	uint timeout_real = _timeout - now;
	if (sceKernelWaitSema(*uid, 1, &timeout_real) < 0)
		return -1;
	return 0;
}

int sem_trywait_soloader (int * uid) {
	uint timeout = 1000;
	if (sceKernelWaitSema(*uid, 1, &timeout) < 0)
		return -1;
	return 0;
}

int sem_wait_soloader (int * uid) {
	if (sceKernelWaitSema(*uid, 1, NULL) < 0)
		return -1;
	return 0;
}

int uname_fake(void *buf) {
	strcpy(buf + 195, "1.0");
	return 0;
}

int scandir_hook(const char *dir, struct android_dirent ***namelist,
	int (*selector) (const struct dirent *),
	int (*compar) (const struct dirent **, const struct dirent **))
{
	return -1;
}

extern void *__aeabi_memset8;
extern void *__aeabi_memset4;
extern void *__aeabi_memset;
extern void *__aeabi_memset8;
extern void *__aeabi_memcpy;
extern void *__aeabi_memcpy4;
extern void *__aeabi_memcpy8;
extern void *__aeabi_memclr;
extern void *__aeabi_memclr4;
extern void *__aeabi_memclr8;

static so_default_dynlib default_dynlib[] = {
	{ "fmaxf", (uintptr_t)&fmaxf },
	{ "fmaf", (uintptr_t)&fmaf },
	{ "fminf", (uintptr_t)&fminf },
	{ "ftruncate", (uintptr_t)&ftruncate },
	{ "pthread_setname_np", (uintptr_t)&ret0 },
	{ "memrchr", (uintptr_t)&memrchr },
	{ "strtok_r", (uintptr_t)&strtok_r },
	{ "glGenRenderbuffers", (uintptr_t)&ret0 },
	{ "glRenderbufferStorage", (uintptr_t)&ret0 },
	{ "glFramebufferRenderbuffer", (uintptr_t)&ret0 },
	{ "glDeleteRenderbuffers", (uintptr_t)&ret0 },
	{ "glBindRenderbuffer", (uintptr_t)&ret0 },
	{ "fsetpos", (uintptr_t)&fsetpos },
	{ "sem_destroy", (uintptr_t)&sem_destroy_soloader },
	{ "sem_getvalue", (uintptr_t)&sem_getvalue_soloader },
	{ "sem_init", (uintptr_t)&sem_init_soloader },
	{ "sem_post", (uintptr_t)&sem_post_soloader },
	{ "sem_timedwait", (uintptr_t)&sem_timedwait_soloader },
	{ "sem_trywait", (uintptr_t)&sem_trywait_soloader },
	{ "sem_wait", (uintptr_t)&sem_wait_soloader },
	{ "alIsExtensionPresent", (uintptr_t)&alIsExtensionPresent },
	{ "alBufferData", (uintptr_t)&alBufferData },
	{ "alDeleteBuffers", (uintptr_t)&alDeleteBuffers },
	{ "alDeleteSources", (uintptr_t)&alDeleteSources },
	{ "alDistanceModel", (uintptr_t)&alDistanceModel },
	{ "alGenBuffers", (uintptr_t)&alGenBuffers },
	{ "alGenSources", (uintptr_t)&alGenSources },
	{ "alGetProcAddress", (uintptr_t)&alGetProcAddress },
	{ "alSpeedOfSound", (uintptr_t)&alSpeedOfSound },
	{ "alDopplerFactor", (uintptr_t)&alDopplerFactor },
	{ "alcIsExtensionPresent", (uintptr_t)&alcIsExtensionPresent },
	{ "alcGetCurrentContext", (uintptr_t)&alcGetCurrentContext },
	{ "alGetBufferi", (uintptr_t)&alGetBufferi },
	{ "alGetError", (uintptr_t)&alGetError },
	{ "alGetString", (uintptr_t)&alGetString },
	{ "alSource3i", (uintptr_t)&alSource3i },
	{ "alSourcefv", (uintptr_t)&alSourcefv },
	{ "alIsSource", (uintptr_t)&alIsSource },
	{ "alGetSourcei", (uintptr_t)&alGetSourcei },
	{ "alGetSourcef", (uintptr_t)&alGetSourcef },
	{ "alIsBuffer", (uintptr_t)&alIsBuffer },
	{ "alListener3f", (uintptr_t)&alListener3f },
	{ "alListenerf", (uintptr_t)&alListenerf },
	{ "alListenerfv", (uintptr_t)&alListenerfv },
	{ "alSource3f", (uintptr_t)&alSource3f },
	{ "alSourcePause", (uintptr_t)&alSourcePause },
	{ "alSourcePlay", (uintptr_t)&alSourcePlay },
	{ "alSourceQueueBuffers", (uintptr_t)&alSourceQueueBuffers },
	{ "alSourceStop", (uintptr_t)&alSourceStop },
	{ "alSourceRewind", (uintptr_t)&alSourceRewind },
	{ "alSourceUnqueueBuffers", (uintptr_t)&alSourceUnqueueBuffers },
	{ "alSourcef", (uintptr_t)&alSourcef },
	{ "alSourcei", (uintptr_t)&alSourcei },
	{ "alcCaptureSamples", (uintptr_t)&alcCaptureSamples },
	{ "alcCaptureStart", (uintptr_t)&alcCaptureStart },
	{ "alcCaptureStop", (uintptr_t)&alcCaptureStop },
	{ "alcCaptureOpenDevice", (uintptr_t)&alcCaptureOpenDevice },
	{ "alcCloseDevice", (uintptr_t)&alcCloseDevice },
	{ "alcCreateContext", (uintptr_t)&alcCreateContext },
	{ "alcGetContextsDevice", (uintptr_t)&alcGetContextsDevice },
	{ "alcGetError", (uintptr_t)&alcGetError },
	{ "alcGetIntegerv", (uintptr_t)&alcGetIntegerv },
	{ "alcGetString", (uintptr_t)&ret0 }, // FIXME
	{ "alcMakeContextCurrent", (uintptr_t)&alcMakeContextCurrent },
	{ "alcDestroyContext", (uintptr_t)&alcDestroyContext },
	{ "alcOpenDevice", (uintptr_t)&alcOpenDevice },
	{ "alcProcessContext", (uintptr_t)&alcProcessContext },
	{ "alcPauseCurrentDevice", (uintptr_t)&ret0 },
	{ "alcResumeCurrentDevice", (uintptr_t)&ret0 },
	{ "alcSuspendContext", (uintptr_t)&alcSuspendContext },
	{ "rename", (uintptr_t)&rename_hook},
	{ "glGetError", (uintptr_t)&ret0},
	{ "glValidateProgram", (uintptr_t)&ret0},
	{ "strtoll_l", (uintptr_t)&strtoll_l},
	{ "strtoull_l", (uintptr_t)&strtoull_l},
	{ "strtold_l", (uintptr_t)&strtold_l},
	{ "wcstoul", (uintptr_t)&wcstoul},
	{ "wcstoll", (uintptr_t)&wcstoll},
	{ "wcstoull", (uintptr_t)&wcstoull},
	{ "wcstof", (uintptr_t)&wcstof},
	{ "wcstod", (uintptr_t)&wcstod},
	{ "wcsnrtombs", (uintptr_t)&wcsnrtombs},
	{ "mbsnrtowcs", (uintptr_t)&mbsnrtowcs},
	{ "mbtowc", (uintptr_t)&mbtowc},
	{ "mbrlen", (uintptr_t)&mbrlen},
	{ "isblank", (uintptr_t)&isblank},
	{ "rmdir", (uintptr_t)&rmdir_hook},
	{ "AAssetManager_openDir", (uintptr_t)&AAssetManager_openDir},
	{ "AAssetDir_getNextFileName", (uintptr_t)&AAssetDir_getNextFileName},
	{ "AAssetDir_close", (uintptr_t)&AAssetDir_close},
	{ "rmdir", (uintptr_t)&rmdir_hook},
	{ "_setjmp", (uintptr_t)&setjmp},
	{ "_longjmp", (uintptr_t)&longjmp},
	{ "glFramebufferTexture2D", (uintptr_t)&glFramebufferTexture2D},
	{ "AAssetManager_open", (uintptr_t)&AAssetManager_open},
	{ "AAsset_close", (uintptr_t)&AAsset_close},
	{ "AAssetManager_fromJava", (uintptr_t)&ret1},
	{ "AAsset_read", (uintptr_t)&AAsset_read},
	{ "AAsset_seek", (uintptr_t)&AAsset_seek},
	{ "AAsset_getLength", (uintptr_t)&AAsset_getLength},
	{ "stdout", (uintptr_t)&fake_stdout },
	{ "stdin", (uintptr_t)&fake_stdout },
	{ "stderr", (uintptr_t)&fake_stdout },
	{ "newlocale", (uintptr_t)&newlocale },
	{ "uselocale", (uintptr_t)&uselocale },
	{ "ov_read", (uintptr_t)&ov_read },
	{ "ov_raw_seek", (uintptr_t)&ov_raw_seek },
	{ "ov_open_callbacks", (uintptr_t)&ov_open_callbacks },
	{ "ov_pcm_total", (uintptr_t)&ov_pcm_total },
	{ "ov_clear", (uintptr_t)&ov_clear },
	{ "exp2f", (uintptr_t)&exp2f },
	{ "__aeabi_l2d", (uintptr_t)&__aeabi_l2d },
	{ "__aeabi_d2ulz", (uintptr_t)&__aeabi_d2ulz },
	{ "__aeabi_ul2d", (uintptr_t)&__aeabi_ul2d },
	{ "__pthread_cleanup_push", (uintptr_t)&ret0 },
	{ "__pthread_cleanup_pop", (uintptr_t)&ret0 },
	{ "sincos", (uintptr_t)&sincos },
	{ "__assert2", (uintptr_t)&__assert2 },
	{ "glTexParameteri", (uintptr_t)&glTexParameteri},
	{ "glReadPixels", (uintptr_t)&glReadPixels},
	{ "glShaderSource", (uintptr_t)&glShaderSource},
	{ "sincosf", (uintptr_t)&sincosf },
	{ "opendir", (uintptr_t)&opendir_fake },
	{ "readdir", (uintptr_t)&readdir_fake },
	{ "closedir", (uintptr_t)&closedir_fake },
	{ "__aeabi_ul2f", (uintptr_t)&__aeabi_ul2f },
	{ "__aeabi_l2f", (uintptr_t)&__aeabi_l2f },
	{ "__aeabi_d2lz", (uintptr_t)&__aeabi_d2lz },
	{ "__aeabi_uldivmod", (uintptr_t)&__aeabi_uldivmod },
	{ "__aeabi_memclr", (uintptr_t)&__aeabi_memclr },
	{ "__aeabi_memclr4", (uintptr_t)&__aeabi_memclr4 },
	{ "__aeabi_memclr8", (uintptr_t)&__aeabi_memclr8 },
	{ "__aeabi_memcpy4", (uintptr_t)&__aeabi_memcpy4 },
	{ "__aeabi_memcpy8", (uintptr_t)&__aeabi_memcpy8 },
	{ "__aeabi_memmove4", (uintptr_t)&memmove },
	{ "__aeabi_memmove8", (uintptr_t)&memmove },
	{ "__aeabi_memcpy", (uintptr_t)&__aeabi_memcpy },
	{ "__aeabi_memmove", (uintptr_t)&memmove },
	{ "__aeabi_memset", (uintptr_t)&__aeabi_memset },
	{ "__aeabi_memset4", (uintptr_t)&__aeabi_memset4 },
	{ "__aeabi_memset8", (uintptr_t)&__aeabi_memset8 },
	{ "__aeabi_atexit", (uintptr_t)&__aeabi_atexit },
	{ "__aeabi_uidiv", (uintptr_t)&__aeabi_uidiv },
	{ "__aeabi_uidivmod", (uintptr_t)&__aeabi_uidivmod },
	{ "__aeabi_ldivmod", (uintptr_t)&__aeabi_ldivmod },
	{ "__aeabi_idivmod", (uintptr_t)&__aeabi_idivmod },
	{ "__aeabi_idiv", (uintptr_t)&__aeabi_idiv },
	{ "__aeabi_ddiv", (uintptr_t)&__aeabi_ddiv },
	{ "__aeabi_dmul", (uintptr_t)&__aeabi_dmul },
	{ "__aeabi_dadd", (uintptr_t)&__aeabi_dadd },
	{ "__aeabi_i2d", (uintptr_t)&__aeabi_i2d },
	{ "__android_log_print", (uintptr_t)&__android_log_print },
	{ "__android_log_vprint", (uintptr_t)&__android_log_vprint },
	{ "__android_log_write", (uintptr_t)&__android_log_write },
	{ "__cxa_atexit", (uintptr_t)&__cxa_atexit },
	{ "__cxa_call_unexpected", (uintptr_t)&__cxa_call_unexpected },
	{ "__cxa_guard_acquire", (uintptr_t)&__cxa_guard_acquire },
	{ "__cxa_guard_release", (uintptr_t)&__cxa_guard_release },
	{ "__cxa_finalize", (uintptr_t)&__cxa_finalize },
	{ "__errno", (uintptr_t)&__errno },
	{ "__strlen_chk", (uintptr_t)&__strlen_chk },
	{ "__gnu_unwind_frame", (uintptr_t)&__gnu_unwind_frame },
	{ "__gnu_Unwind_Find_exidx", (uintptr_t)&ret0 },
	{ "dl_unwind_find_exidx", (uintptr_t)&ret0 },
	{ "__sF", (uintptr_t)&__sF_fake },
	{ "__stack_chk_fail", (uintptr_t)&__stack_chk_fail },
	{ "__stack_chk_guard", (uintptr_t)&__stack_chk_guard_fake },
	{ "abort", (uintptr_t)&abort_hook },
	{ "access", (uintptr_t)&access_hook },
	{ "acos", (uintptr_t)&acos },
	{ "acosh", (uintptr_t)&acosh },
	{ "asctime", (uintptr_t)&asctime },
	{ "acosf", (uintptr_t)&acosf },
	{ "asin", (uintptr_t)&asin },
	{ "asinh", (uintptr_t)&asinh },
	{ "asinf", (uintptr_t)&asinf },
	{ "atan", (uintptr_t)&atan },
	{ "atanh", (uintptr_t)&atanh },
	{ "atan2", (uintptr_t)&atan2 },
	{ "atan2f", (uintptr_t)&atan2f },
	{ "atanf", (uintptr_t)&atanf },
	{ "atoi", (uintptr_t)&atoi },
	{ "atol", (uintptr_t)&atol },
	{ "atoll", (uintptr_t)&atoll },
	{ "basename", (uintptr_t)&basename },
	{ "bsd_signal", (uintptr_t)&ret0 },
	{ "bsearch", (uintptr_t)&bsearch },
	{ "btowc", (uintptr_t)&btowc },
	{ "calloc", (uintptr_t)&calloc },
	{ "ceil", (uintptr_t)&ceil },
	{ "ceilf", (uintptr_t)&ceilf },
	{ "chdir", (uintptr_t)&chdir_hook },
	{ "clearerr", (uintptr_t)&clearerr },
	{ "clock", (uintptr_t)&clock },
	{ "clock_gettime", (uintptr_t)&clock_gettime_hook },
	{ "close", (uintptr_t)&close },
	{ "cos", (uintptr_t)&cos },
	{ "cosf", (uintptr_t)&cosf },
	{ "cosh", (uintptr_t)&cosh },
	{ "crc32", (uintptr_t)&crc32 },
	{ "deflate", (uintptr_t)&deflate },
	{ "deflateEnd", (uintptr_t)&deflateEnd },
	{ "deflateInit_", (uintptr_t)&deflateInit_ },
	{ "deflateInit2_", (uintptr_t)&deflateInit2_ },
	{ "deflateReset", (uintptr_t)&deflateReset },
	{ "dlopen", (uintptr_t)&ret0 },
	{ "dlsym", (uintptr_t)&dlsym_hook },
	{ "exit", (uintptr_t)&exit },
	{ "exp", (uintptr_t)&exp },
	{ "exp2", (uintptr_t)&exp2 },
	{ "expf", (uintptr_t)&expf },
	{ "fabsf", (uintptr_t)&fabsf },
	{ "fclose", (uintptr_t)&fclose },
	{ "fcntl", (uintptr_t)&ret0 },
	{ "mktime", (uintptr_t)&mktime },
	{ "feof", (uintptr_t)&feof },
	{ "ferror", (uintptr_t)&ferror },
	{ "fflush", (uintptr_t)&ret0 },
	{ "fgets", (uintptr_t)&fgets },
	{ "floor", (uintptr_t)&floor },
	{ "fileno", (uintptr_t)&fileno },
	{ "floorf", (uintptr_t)&floorf },
	{ "fmod", (uintptr_t)&fmod },
	{ "fmodf", (uintptr_t)&fmodf },
	{ "fopen", (uintptr_t)&fopen_hook },
	{ "open", (uintptr_t)&open_hook },
	{ "fprintf", (uintptr_t)&fprintf },
	{ "fputc", (uintptr_t)&ret0 },
	{ "fputs", (uintptr_t)&ret0 },
	{ "fread", (uintptr_t)&fread },
	{ "free", (uintptr_t)&free },
	{ "frexp", (uintptr_t)&frexp },
	{ "frexpf", (uintptr_t)&frexpf },
	{ "fscanf", (uintptr_t)&fscanf },
	{ "fseek", (uintptr_t)&fseek },
	{ "fseeko", (uintptr_t)&fseeko },
	{ "fstat", (uintptr_t)&fstat_hook },
	{ "ftell", (uintptr_t)&ftell },
	{ "ftello", (uintptr_t)&ftello },
	{ "fwrite", (uintptr_t)&fwrite },
	{ "getc", (uintptr_t)&getc },
	{ "gettid", (uintptr_t)&ret0 },
	{ "getpid", (uintptr_t)&ret0 },
	{ "getcwd", (uintptr_t)&getcwd_hook },
	{ "getenv", (uintptr_t)&ret0 },
	{ "getwc", (uintptr_t)&getwc },
	{ "gettimeofday", (uintptr_t)&gettimeofday },
	{ "gzopen", (uintptr_t)&gzopen },
	{ "inflate", (uintptr_t)&inflate },
	{ "inflateEnd", (uintptr_t)&inflateEnd },
	{ "inflateInit_", (uintptr_t)&inflateInit_ },
	{ "inflateInit2_", (uintptr_t)&inflateInit2_ },
	{ "inflateReset", (uintptr_t)&inflateReset },
	{ "isascii", (uintptr_t)&isascii },
	{ "isalnum", (uintptr_t)&isalnum },
	{ "isalpha", (uintptr_t)&isalpha },
	{ "iscntrl", (uintptr_t)&iscntrl },
	{ "isdigit", (uintptr_t)&isdigit },
	{ "islower", (uintptr_t)&islower },
	{ "ispunct", (uintptr_t)&ispunct },
	{ "isprint", (uintptr_t)&isprint },
	{ "isspace", (uintptr_t)&isspace },
	{ "isupper", (uintptr_t)&isupper },
	{ "iswalpha", (uintptr_t)&iswalpha },
	{ "iswcntrl", (uintptr_t)&iswcntrl },
	{ "iswctype", (uintptr_t)&iswctype },
	{ "iswdigit", (uintptr_t)&iswdigit },
	{ "iswdigit", (uintptr_t)&iswdigit },
	{ "iswlower", (uintptr_t)&iswlower },
	{ "iswprint", (uintptr_t)&iswprint },
	{ "iswpunct", (uintptr_t)&iswpunct },
	{ "iswspace", (uintptr_t)&iswspace },
	{ "iswupper", (uintptr_t)&iswupper },
	{ "iswxdigit", (uintptr_t)&iswxdigit },
	{ "isxdigit", (uintptr_t)&isxdigit },
	{ "ldexp", (uintptr_t)&ldexp },
	{ "ldexpf", (uintptr_t)&ldexpf },
	{ "localtime", (uintptr_t)&localtime },
	{ "localtime_r", (uintptr_t)&localtime_r },
	{ "log", (uintptr_t)&log },
	{ "logf", (uintptr_t)&logf },
	{ "log10", (uintptr_t)&log10 },
	{ "log10f", (uintptr_t)&log10f },
	{ "longjmp", (uintptr_t)&longjmp },
	{ "lrand48", (uintptr_t)&lrand48 },
	{ "lrint", (uintptr_t)&lrint },
	{ "lrintf", (uintptr_t)&lrintf },
	{ "lseek", (uintptr_t)&lseek },
	{ "lseek64", (uintptr_t)&lseek64 },
	{ "malloc", (uintptr_t)&malloc },
	{ "mbrtowc", (uintptr_t)&mbrtowc },
	{ "memalign", (uintptr_t)&memalign },
	{ "memchr", (uintptr_t)&sceClibMemchr },
	{ "memcmp", (uintptr_t)&memcmp },
	{ "memcpy", (uintptr_t)&sceClibMemcpy },
	{ "memmove", (uintptr_t)&memmove },
	{ "memset", (uintptr_t)&sceClibMemset },
	{ "mkdir", (uintptr_t)&mkdir_hook },
	{ "modf", (uintptr_t)&modf },
	{ "modff", (uintptr_t)&modff },
	{ "pow", (uintptr_t)&pow },
	{ "powf", (uintptr_t)&powf },
	{ "printf", (uintptr_t)&printf },
	{ "pthread_join", (uintptr_t)&pthread_join_soloader },
	{ "pthread_attr_destroy", (uintptr_t)&pthread_attr_destroy_soloader },
	{ "pthread_attr_getstack", (uintptr_t)&pthread_attr_getstack_soloader },
	{ "pthread_attr_init", (uintptr_t) &pthread_attr_init_soloader },
	{ "pthread_attr_setdetachstate", (uintptr_t) &pthread_attr_setdetachstate_soloader },
	{ "pthread_attr_setschedparam", (uintptr_t)&pthread_attr_setschedparam_soloader },
	{ "pthread_attr_getschedparam", (uintptr_t)&pthread_attr_getschedparam_soloader },
	{ "pthread_attr_setschedpolicy", (uintptr_t)&ret0 },
	{ "pthread_attr_setstack", (uintptr_t)&pthread_attr_setstack_soloader },
	{ "pthread_attr_setstacksize", (uintptr_t) &pthread_attr_setstacksize_soloader },
	{ "pthread_attr_getstacksize", (uintptr_t) &pthread_attr_getstacksize_soloader },
	{ "pthread_cond_broadcast", (uintptr_t) &pthread_cond_broadcast_soloader },
	{ "pthread_cond_destroy", (uintptr_t) &pthread_cond_destroy_soloader },
	{ "pthread_cond_init", (uintptr_t) &pthread_cond_init_soloader },
	{ "pthread_cond_signal", (uintptr_t) &pthread_cond_signal_soloader },
	{ "pthread_cond_timedwait", (uintptr_t) &pthread_cond_timedwait_soloader },
	{ "pthread_cond_wait", (uintptr_t) &pthread_cond_wait_soloader },
	{ "pthread_condattr_init", (uintptr_t) &pthread_condattr_init_soloader },
	{ "pthread_condattr_destroy", (uintptr_t) &pthread_condattr_destroy_soloader },
	{ "pthread_create", (uintptr_t) &pthread_create_soloader },
	{ "pthread_detach", (uintptr_t) &pthread_detach_soloader },
	{ "pthread_equal", (uintptr_t) &pthread_equal_soloader },
	{ "pthread_exit", (uintptr_t) &pthread_exit },
	{ "pthread_getattr_np", (uintptr_t) &pthread_getattr_np_soloader },
	{ "pthread_getschedparam", (uintptr_t) &pthread_getschedparam_soloader },
	{ "pthread_getspecific", (uintptr_t)&pthread_getspecific },
	{ "pthread_key_create", (uintptr_t)&pthread_key_create },
	{ "pthread_key_delete", (uintptr_t)&pthread_key_delete },
	{ "pthread_mutex_destroy", (uintptr_t) &pthread_mutex_destroy_soloader },
	{ "pthread_mutex_init", (uintptr_t) &pthread_mutex_init_soloader },
	{ "pthread_mutex_lock", (uintptr_t) &pthread_mutex_lock_soloader },
	{ "pthread_mutex_trylock", (uintptr_t) &pthread_mutex_trylock_soloader},
	{ "pthread_mutex_unlock", (uintptr_t) &pthread_mutex_unlock_soloader },
	{ "pthread_mutexattr_destroy", (uintptr_t) &pthread_mutexattr_destroy_soloader},
	{ "pthread_mutexattr_init", (uintptr_t) &pthread_mutexattr_init_soloader},
	{ "pthread_mutexattr_setpshared", (uintptr_t) &pthread_mutexattr_setpshared_soloader},
	{ "pthread_mutexattr_settype", (uintptr_t) &pthread_mutexattr_settype_soloader},
	{ "pthread_once", (uintptr_t)&pthread_once },
	{ "pthread_self", (uintptr_t) &pthread_self },
	{ "pthread_setschedparam", (uintptr_t) &pthread_setschedparam_soloader },
	{ "pthread_setspecific", (uintptr_t)&pthread_setspecific },
	{ "sched_get_priority_min", (uintptr_t)&ret0 },
	{ "sched_get_priority_max", (uintptr_t)&ret99 },
	{ "putc", (uintptr_t)&putc },
	{ "puts", (uintptr_t)&puts },
	{ "putwc", (uintptr_t)&putwc },
	{ "qsort", (uintptr_t)&qsort },
	{ "rand", (uintptr_t)&rand },
	{ "read", (uintptr_t)&read },
	{ "realpath", (uintptr_t)&realpath },
	{ "realloc", (uintptr_t)&realloc },
	{ "roundf", (uintptr_t)&roundf },
	{ "rint", (uintptr_t)&rint },
	{ "rintf", (uintptr_t)&rintf },
	{ "scandir", (uintptr_t)&scandir_hook },
	{ "setenv", (uintptr_t)&ret0 },
	{ "setjmp", (uintptr_t)&setjmp },
	{ "setlocale", (uintptr_t)&ret0 },
	{ "setvbuf", (uintptr_t)&setvbuf },
	{ "sin", (uintptr_t)&sin },
	{ "sinf", (uintptr_t)&sinf },
	{ "sinh", (uintptr_t)&sinh },
	{ "snprintf", (uintptr_t)&snprintf },
	{ "sprintf", (uintptr_t)&sprintf },
	{ "sqrt", (uintptr_t)&sqrt },
	{ "sqrtf", (uintptr_t)&sqrtf },
	{ "srand", (uintptr_t)&srand },
	{ "srand48", (uintptr_t)&srand48 },
	{ "sscanf", (uintptr_t)&sscanf },
	{ "stat", (uintptr_t)&stat_hook },
	{ "lstat", (uintptr_t)&lstat_hook },
	{ "strcasecmp", (uintptr_t)&strcasecmp },
	{ "strcasestr", (uintptr_t)&strstr },
	{ "strcat", (uintptr_t)&strcat },
	{ "strchr", (uintptr_t)&strchr },
	{ "strcmp", (uintptr_t)&strcmp },
	{ "strcoll", (uintptr_t)&strcoll },
	{ "strcpy", (uintptr_t)&strcpy },
	{ "strcspn", (uintptr_t)&strcspn },
	{ "strdup", (uintptr_t)&strdup },
	{ "strerror", (uintptr_t)&strerror },
	{ "strerror_r", (uintptr_t)&strerror_r_hook },
	{ "strftime", (uintptr_t)&strftime },
	{ "strlcpy", (uintptr_t)&strlcpy },
	{ "strlen", (uintptr_t)&strlen },
	{ "strncasecmp", (uintptr_t)&strncasecmp },
	{ "strncat", (uintptr_t)&strncat },
	{ "strncmp", (uintptr_t)&strncmp },
	{ "strncpy", (uintptr_t)&strncpy },
	{ "strpbrk", (uintptr_t)&strpbrk },
	{ "strrchr", (uintptr_t)&strrchr },
	{ "strstr", (uintptr_t)&strstr },
	{ "strtod", (uintptr_t)&strtod },
	{ "strtol", (uintptr_t)&strtol },
	{ "strtoul", (uintptr_t)&strtoul },
	{ "strtoll", (uintptr_t)&strtoll },
	{ "strtoull", (uintptr_t)&strtoull },
	{ "strtok", (uintptr_t)&strtok },
	{ "strxfrm", (uintptr_t)&strxfrm },
	{ "sysconf", (uintptr_t)&ret0 },
	{ "tan", (uintptr_t)&tan },
	{ "tanf", (uintptr_t)&tanf },
	{ "tanh", (uintptr_t)&tanh },
	{ "time", (uintptr_t)&time },
	{ "tolower", (uintptr_t)&tolower },
	{ "toupper", (uintptr_t)&toupper },
	{ "towlower", (uintptr_t)&towlower },
	{ "towupper", (uintptr_t)&towupper },
	{ "uname", (uintptr_t)&uname_fake },
	{ "ungetc", (uintptr_t)&ungetc },
	{ "ungetwc", (uintptr_t)&ungetwc },
	{ "usleep", (uintptr_t)&usleep },
	{ "vasprintf", (uintptr_t)&vasprintf },
	{ "vfprintf", (uintptr_t)&vfprintf },
	{ "vprintf", (uintptr_t)&vprintf },
	{ "vsnprintf", (uintptr_t)&vsnprintf },
	{ "vsscanf", (uintptr_t)&vsscanf },
	{ "vsprintf", (uintptr_t)&vsprintf },
	{ "vswprintf", (uintptr_t)&vswprintf },
	{ "wcrtomb", (uintptr_t)&wcrtomb },
	{ "wcscoll", (uintptr_t)&wcscoll },
	{ "wcscmp", (uintptr_t)&wcscmp },
	{ "wcsncpy", (uintptr_t)&wcsncpy },
	{ "wcsftime", (uintptr_t)&wcsftime },
	{ "wcslen", (uintptr_t)&wcslen },
	{ "wcsxfrm", (uintptr_t)&wcsxfrm },
	{ "wctob", (uintptr_t)&wctob },
	{ "wctype", (uintptr_t)&wctype },
	{ "wmemchr", (uintptr_t)&wmemchr },
	{ "wmemcmp", (uintptr_t)&wmemcmp },
	{ "wmemcpy", (uintptr_t)&wmemcpy },
	{ "wmemmove", (uintptr_t)&wmemmove },
	{ "wmemset", (uintptr_t)&wmemset },
	{ "write", (uintptr_t)&write },
	{ "sigaction", (uintptr_t)&ret0 },
	{ "zlibVersion", (uintptr_t)&zlibVersion },
	{ "unlink", (uintptr_t)&unlink_hook },
	{ "Android_JNI_GetEnv", (uintptr_t)&Android_JNI_GetEnv },
	{ "nanosleep", (uintptr_t)&nanosleep_hook }, // FIXME
	{ "raise", (uintptr_t)&raise },
	{ "posix_memalign", (uintptr_t)&posix_memalign },
	{ "swprintf", (uintptr_t)&swprintf },
	{ "wcscpy", (uintptr_t)&wcscpy },
	{ "wcscat", (uintptr_t)&wcscat },
	{ "wcstombs", (uintptr_t)&wcstombs },
	{ "wcsstr", (uintptr_t)&wcsstr },
	{ "compress", (uintptr_t)&compress },
	{ "uncompress", (uintptr_t)&uncompress },
	{ "atof", (uintptr_t)&atof },
	{ "trunc", (uintptr_t)&trunc },
	{ "round", (uintptr_t)&round },
	{ "llrintf", (uintptr_t)&llrintf },
	{ "llrint", (uintptr_t)&llrint },
	{ "remove", (uintptr_t)&remove_hook },
	{ "__system_property_get", (uintptr_t)&ret0 },
	{ "strnlen", (uintptr_t)&strnlen }
};

int check_kubridge(void) {
	int search_unk[2];
	return _vshKernelSearchModuleByName("kubridge", search_unk);
}

enum MethodIDs {
	UNKNOWN = 0,
	INIT,
	IS_CONNECTED_TO_NETWORK,
	GET_DEVICE_LANGUAGE,
	END_SESSION
} MethodIDs;

typedef struct {
	char *name;
	enum MethodIDs id;
} NameToMethodID;

static NameToMethodID name_to_method_ids[] = {
	{ "<init>", INIT },
	{ "IsConnectedToNetwork", IS_CONNECTED_TO_NETWORK },
	{ "GetDeviceLanguage", GET_DEVICE_LANGUAGE },
	{ "onEndSession", END_SESSION }
};

int GetMethodID(void *env, void *class, const char *name, const char *sig) {
	dlog("GetMethodID: %s\n", name);
	for (int i = 0; i < sizeof(name_to_method_ids) / sizeof(NameToMethodID); i++) {
		if (strcmp(name, name_to_method_ids[i].name) == 0) {
			return name_to_method_ids[i].id;
		}
	}

	return UNKNOWN;
}

int GetStaticMethodID(void *env, void *class, const char *name, const char *sig) {
	dlog("GetStaticMethodID: %s\n", name);
	for (int i = 0; i < sizeof(name_to_method_ids) / sizeof(NameToMethodID); i++) {
		if (strcmp(name, name_to_method_ids[i].name) == 0)
			return name_to_method_ids[i].id;
	}

	return UNKNOWN;
}

int end_session = 0;

void CallStaticVoidMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
	switch (methodID) {
	case END_SESSION:
		end_session = 1;
		break;
	}
}

int CallStaticBooleanMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
	switch (methodID) {
	case IS_CONNECTED_TO_NETWORK:
		return 0;
	default:
		return 0;
	}
}

int GetDeviceLanguage() {
	int language = 0;
	sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &language);
	switch (language) {
		case SCE_SYSTEM_PARAM_LANG_FRENCH: return 1;
		case SCE_SYSTEM_PARAM_LANG_GERMAN: return 2;
		case SCE_SYSTEM_PARAM_LANG_ITALIAN: return 3;
		case SCE_SYSTEM_PARAM_LANG_SPANISH: return 4;
		case SCE_SYSTEM_PARAM_LANG_PORTUGUESE_PT: return 5;
		case SCE_SYSTEM_PARAM_LANG_RUSSIAN: return 6;
		case SCE_SYSTEM_PARAM_LANG_JAPANESE: return 7;
		case SCE_SYSTEM_PARAM_LANG_CHINESE_S: return 8;
		case SCE_SYSTEM_PARAM_LANG_KOREAN: return 9;
		case SCE_SYSTEM_PARAM_LANG_TURKISH: return 10;
		default: return 0;
	}
}

int CallStaticIntMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
	switch (methodID) {
	case GET_DEVICE_LANGUAGE:
		return GetDeviceLanguage();
	default:
		return 0;
	}
}

int64_t CallStaticLongMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
	switch (methodID) {
	default:
		return 0;
	}
}

uint64_t CallLongMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
	return -1;
}

void *FindClass(void) {
	return (void *)0x41414141;
}

void *NewGlobalRef(void *env, char *str) {
	return (void *)0x42424242;
}

void DeleteGlobalRef(void *env, char *str) {
}

void *NewObjectV(void *env, void *clazz, int methodID, uintptr_t args) {
	return (void *)0x43434343;
}

void *GetObjectClass(void *env, void *obj) {
	return (void *)0x44444444;
}

char *NewStringUTF(void *env, char *bytes) {
	return bytes;
}

char *GetStringUTFChars(void *env, char *string, int *isCopy) {
	return string;
}

size_t GetStringUTFLength(void *env, char *string) {
	return strlen(string);
}

int GetJavaVM(void *env, void **vm) {
	*vm = fake_vm;
	return 0;
}

int GetFieldID(void *env, void *clazz, const char *name, const char *sig) {
	return 0;
}

int GetBooleanField(void *env, void *obj, int fieldID) {
	return 1;
}

void *GetObjectArrayElement(void *env, uint8_t *obj, int idx) {
	return NULL;
}

int CallBooleanMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
	switch (methodID) {
	default:
		return 0;
	}
}

void *CallObjectMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
	switch (methodID) {
	default:
		return NULL;
	}
}

int CallIntMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
	switch (methodID) {
	default:
		return 0;
	}
}

void CallVoidMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
	switch (methodID) {
	default:
		break;
	}
}

int GetStaticFieldID(void *env, void *clazz, const char *name, const char *sig) {
	return 0;
}

void *GetStaticObjectField(void *env, void *clazz, int fieldID) {
	switch (fieldID) {
	default:
		return NULL;
	}
}

void GetStringUTFRegion(void *env, char *str, size_t start, size_t len, char *buf) {
	sceClibMemcpy(buf, &str[start], len);
	buf[len] = 0;
}

int GetIntField(void *env, void *obj, int fieldID) { return 0; }

float GetFloatField(void *env, void *obj, int fieldID) {
	switch (fieldID) {
	default:
		return 0.0f;
	}
}

float CallStaticFloatMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
	switch (methodID) {
	default:
		if (methodID != UNKNOWN) {
			dlog("CallStaticDoubleMethodV(%d)\n", methodID);
		}
		return 0;
	}
}

int GetArrayLength(void *env, void *array) {
	dlog("GetArrayLength returned %d\n", *(int *)array);
	return *(int *)array;
}

void patch_game(void) {
	hook_addr(so_symbol(&main_mod, "S3DClient_InstallCurrentUserEventHook"), (uintptr_t)&ret0);

	// Display Control Settings screen only on start (first level)
	uint32_t nop = 0xE1A00000;
	kuKernelCpuUnrestrictedMemcpy((void *)(main_mod.text_base + 0x1B1CE0), &nop, sizeof(nop));
	kuKernelCpuUnrestrictedMemcpy((void *)(main_mod.text_base + 0x2019E8), &nop, sizeof(nop));
	kuKernelCpuUnrestrictedMemcpy((void *)(main_mod.text_base + 0x201D0C), &nop, sizeof(nop));
	kuKernelCpuUnrestrictedMemcpy((void *)(main_mod.text_base + 0x201EBC), &nop, sizeof(nop));
	kuKernelCpuUnrestrictedMemcpy((void *)(main_mod.text_base + 0x201F94), &nop, sizeof(nop));
}

uint8_t is_lowend = 0;

int inited = 0;
GLuint splash_tex;

int SplashRender() {
	if (!inited) {
		inited = 1;
		splash_img img = get_splashscreen();
		glGenTextures(1, &splash_tex);
		glBindTexture(GL_TEXTURE_2D, splash_tex	);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.buf);
		free(img.buf);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		glOrtho(0.0f, 960.0f, 544.0f, 0.0f, 0.0f, 1.0f);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
	}

	float verts[] = {0.0f, 0.0f, 0.0f, 544.0f, 960.0f, 0.0f, 960.0, 544.0f};
	float texs[] = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f};

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, verts);
	glTexCoordPointer(2, GL_FLOAT, 0, texs);
	glBindTexture(GL_TEXTURE_2D, splash_tex);
	glEnable(GL_TEXTURE_2D);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	return 0;
}

void *pthread_main(void *arg) {
	int (* JNI_OnLoad) (void *vm) = (void *)so_symbol(&main_mod, "JNI_OnLoad");
	int (* Engine_Initialize) (void *env) = (void *)so_symbol(&main_mod, "Java_com_ubisoft_pop2_S3DRenderer_engineInitialize");
	int (* Engine_RunOneFrame) (void *env) = (void *)so_symbol(&main_mod, "Java_com_ubisoft_pop2_S3DRenderer_engineRunOneFrame");
	int (* Engine_DidPassFirstFrame) (void *env) = (void *)so_symbol(&main_mod, "Java_com_ubisoft_pop2_S3DRenderer_engineDidPassFirstFrame");
	int (* Engine_SetSystemVersion) (void *env, void *obj, char *ver) = (void *)so_symbol(&main_mod, "Java_com_ubisoft_pop2_S3DRenderer_engineSetSystemVersion");
	int (* Engine_SetDirectories) (void *env, void *obj, char *cache_path, char *home_path, char *pack_path) = (void *)so_symbol(&main_mod, "Java_com_ubisoft_pop2_S3DRenderer_engineSetDirectories");
	int (* Engine_Pause) (void *env, void *obj, int pause) = (void *)so_symbol(&main_mod, "Java_com_ubisoft_pop2_S3DRenderer_enginePause");
	int (* Engine_Touch) (void *env, void *obj, int state, float x, float y, int state2, float x2, float y2, int state3, float x3, float y3, int state4, float x4, float y4, int state5, float x5, float y5) = (void *)so_symbol(&main_mod, "Java_com_ubisoft_pop2_S3DRenderer_engineOnTouchesChange");
	int (* Engine_SurfaceCreated) () = (void *)so_symbol(&main_mod, "Java_com_ubisoft_pop2_S3DRenderer_engineOnSurfaceCreated");
	int (* Engine_SurfaceChanged) (void *env, void *obj, int width, int height) = (void *)so_symbol(&main_mod, "Java_com_ubisoft_pop2_S3DRenderer_engineOnSurfaceChanged");
	int (* Engine_OnMouseMove) (void *env, void *obj, float x, float y) = (void *)so_symbol(&main_mod, "Java_com_ubisoft_pop2_S3DRenderer_engineOnMouseMove");
	int (* Engine_OnMouseUp) (void *env, void *obj, float x, float y) = (void *)so_symbol(&main_mod, "Java_com_ubisoft_pop2_S3DRenderer_engineOnMouseButtonUp");
	int (* Engine_OnMouseDown) (void *env, void *obj, float x, float y) = (void *)so_symbol(&main_mod, "Java_com_ubisoft_pop2_S3DRenderer_engineOnMouseButtonDown");
	int (* Engine_OnKeyboardKeyUp) (void *env, void *obj, int x, int y, int b) = (void *)so_symbol(&main_mod, "Java_com_ubisoft_pop2_S3DRenderer_engineOnKeyboardKeyUp");
	int (* Engine_OnKeyboardKeyDown) (void *env, void *obj, int x, int y, int b) = (void *)so_symbol(&main_mod, "Java_com_ubisoft_pop2_S3DRenderer_engineOnKeyboardKeyDown");

	sceClibPrintf("JNI_OnLoad\n");
	JNI_OnLoad(fake_vm);

	Engine_SetSystemVersion(fake_env, NULL, "v.1.1-vita");
	Engine_SetDirectories(fake_env, NULL, "ux0:data/pop2", "ux0:data/pop2", "ux0:data/pop2");

	Engine_SurfaceCreated();
	Engine_SurfaceChanged(fake_env, NULL, SCREEN_W, SCREEN_H);

	sceClibPrintf("Engine_Initialize\n");
	Engine_Initialize(fake_env);

	Engine_Pause(fake_env, NULL, 0);

	sceClibPrintf("Entering main loop\n");

	#define handleKey(btn, code) \
		if (((pad.buttons & btn) == btn) && !((oldpad & btn) == btn)) { \
			Engine_OnKeyboardKeyDown(fake_env, NULL, code, 0, 1); \
		} else if (((oldpad & btn) == btn) && !((pad.buttons & btn) == btn)) { \
			Engine_OnKeyboardKeyUp(fake_env, NULL, code, 0, 0); \
		}

	int lastX = -1, lastY = -1;
	for (;;) {
		SceTouchData touch;
		sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);

		int ts[5];
		float tx[5];
		float ty[5];

		for (int j = 0; j < 6; j++) {
			if (j == 0) {
				if (touch.reportNum) {
					int x = (int)(touch.report[0].x * 0.5f);
					int y = (int)(touch.report[0].y * 0.5f);
					if (lastX == -1 || lastY == -1) {
						Engine_OnMouseDown(fake_env, NULL, (float)x, (float)y);
					} else {
						Engine_OnMouseMove(fake_env, NULL, (float)x, (float)y);
					}
					lastX = x;
					lastY = y;
				} else {
					Engine_OnMouseUp(fake_env, NULL, (float)lastX, (float)lastY);
					lastX = lastY = -1;
				}
			} else {
				int i = j - 1;
				if (i < touch.reportNum) {
					ts[i] = 1;
					tx[i] = touch.report[i].x * 0.5f;
					ty[i] = touch.report[i].y * 0.5f;
				} else {
					ts[i] = 0;
					tx[i] = 0.0f;
					ty[i] = 0.0f;
				}
			}
		}
		Engine_Touch(fake_env, NULL, ts[0], tx[0], ty[0], ts[1], tx[1], ty[1], ts[2], tx[2], ty[2], ts[3], tx[3], ty[3], ts[4], tx[4], ty[4]);

		SceCtrlData pad;
		static uint32_t oldpad = 0;
		sceCtrlPeekBufferPositive(0, &pad, 1);
		handleKey(SCE_CTRL_CIRCLE, 18);
		handleKey(SCE_CTRL_UP, 19);
		handleKey(SCE_CTRL_DOWN, 20);
		handleKey(SCE_CTRL_LEFT, 21);
		handleKey(SCE_CTRL_RIGHT, 22);
		handleKey(SCE_CTRL_CROSS, 23);
		handleKey(SCE_CTRL_SQUARE, 99);
		handleKey(SCE_CTRL_TRIANGLE, 100);
		handleKey(SCE_CTRL_LTRIGGER, 102);
		handleKey(SCE_CTRL_RTRIGGER, 103);
		handleKey(SCE_CTRL_START, 108);
		oldpad = pad.buttons;

		if (!Engine_DidPassFirstFrame(fake_env)) {
			SplashRender();
			vglSwapBuffers(GL_FALSE);
		}

		Engine_RunOneFrame(fake_env);
		vglSwapBuffers(GL_FALSE);

		if (end_session) {
			exit(0);
			break;
		}
	}

	return NULL;
}

int main(int argc, char *argv[]) {
	SceAppUtilInitParam init_param;
	SceAppUtilBootParam boot_param;
	memset(&init_param, 0, sizeof(SceAppUtilInitParam));
	memset(&boot_param, 0, sizeof(SceAppUtilBootParam));
	sceAppUtilInit(&init_param, &boot_param);

	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);

	scePowerSetArmClockFrequency(444);
	scePowerSetBusClockFrequency(222);
	scePowerSetGpuClockFrequency(222);
	scePowerSetGpuXbarClockFrequency(166);

	if (check_kubridge() < 0) {
		fatal_error("Error kubridge.skprx is not installed.");
	}

	if (!file_exists("ur0:/data/libshacccg.suprx") && !file_exists("ur0:/data/external/libshacccg.suprx")) {
		fatal_error("Error libshacccg.suprx is not installed.");
	}

	char fname[30];
	sprintf(data_path, "ux0:data/pop2");

	sceClibPrintf("Loading libS3DClient\n");
	sprintf(fname, "%s/libS3DClient.so", data_path);
	if (so_file_load(&main_mod, fname, LOAD_ADDRESS) < 0) {
		fatal_error("Error could not load %s.", fname);
	}
	so_relocate(&main_mod);
	so_resolve(&main_mod, default_dynlib, sizeof(default_dynlib), 0);

	vglSetSemanticBindingMode(VGL_MODE_POSTPONED);
	vglUseTripleBuffering(GL_FALSE);
	vglSetParamBufferSize(6 * 1024 * 1024);
	vglInitWithCustomThreshold(0, SCREEN_W, SCREEN_H, 6 * 1024 * 1024, 0, 0, 0, SCE_GXM_MULTISAMPLE_2X);

	patch_game();
	so_flush_caches(&main_mod);
	so_initialize(&main_mod);

	memset(fake_vm, 'A', sizeof(fake_vm));
	*(uintptr_t *)(fake_vm + 0x00) = (uintptr_t)fake_vm; // just point to itself...
	*(uintptr_t *)(fake_vm + 0x10) = (uintptr_t)ret0;
	*(uintptr_t *)(fake_vm + 0x14) = (uintptr_t)ret0;
	*(uintptr_t *)(fake_vm + 0x18) = (uintptr_t)GetEnv;

	memset(fake_env, 'A', sizeof(fake_env));
	*(uintptr_t *)(fake_env + 0x00) = (uintptr_t)fake_env; // just point to itself...
	*(uintptr_t *)(fake_env + 0x18) = (uintptr_t)FindClass;
	*(uintptr_t *)(fake_env + 0x4C) = (uintptr_t)ret0; // PushLocalFrame
	*(uintptr_t *)(fake_env + 0x50) = (uintptr_t)ret0; // PopLocalFrame
	*(uintptr_t *)(fake_env + 0x54) = (uintptr_t)NewGlobalRef;
	*(uintptr_t *)(fake_env + 0x58) = (uintptr_t)DeleteGlobalRef;
	*(uintptr_t *)(fake_env + 0x5C) = (uintptr_t)ret0; // DeleteLocalRef
	*(uintptr_t *)(fake_env + 0x74) = (uintptr_t)NewObjectV;
	*(uintptr_t *)(fake_env + 0x7C) = (uintptr_t)GetObjectClass;
	*(uintptr_t *)(fake_env + 0x84) = (uintptr_t)GetMethodID;
	*(uintptr_t *)(fake_env + 0x8C) = (uintptr_t)CallObjectMethodV;
	*(uintptr_t *)(fake_env + 0x98) = (uintptr_t)CallBooleanMethodV;
	*(uintptr_t *)(fake_env + 0xC8) = (uintptr_t)CallIntMethodV;
	*(uintptr_t *)(fake_env + 0xD4) = (uintptr_t)CallLongMethodV;
	*(uintptr_t *)(fake_env + 0xF8) = (uintptr_t)CallVoidMethodV;
	*(uintptr_t *)(fake_env + 0x178) = (uintptr_t)GetFieldID;
	*(uintptr_t *)(fake_env + 0x17C) = (uintptr_t)GetBooleanField;
	*(uintptr_t *)(fake_env + 0x190) = (uintptr_t)GetIntField;
	*(uintptr_t *)(fake_env + 0x198) = (uintptr_t)GetFloatField;
	*(uintptr_t *)(fake_env + 0x1C4) = (uintptr_t)GetStaticMethodID;
	*(uintptr_t *)(fake_env + 0x1D8) = (uintptr_t)CallStaticBooleanMethodV;
	*(uintptr_t *)(fake_env + 0x208) = (uintptr_t)CallStaticIntMethodV;
	*(uintptr_t *)(fake_env + 0x21C) = (uintptr_t)CallStaticLongMethodV;
	*(uintptr_t *)(fake_env + 0x220) = (uintptr_t)CallStaticFloatMethodV;
	*(uintptr_t *)(fake_env + 0x238) = (uintptr_t)CallStaticVoidMethodV;
	*(uintptr_t *)(fake_env + 0x240) = (uintptr_t)GetStaticFieldID;
	*(uintptr_t *)(fake_env + 0x244) = (uintptr_t)GetStaticObjectField;
	*(uintptr_t *)(fake_env + 0x29C) = (uintptr_t)NewStringUTF;
	*(uintptr_t *)(fake_env + 0x2A0) = (uintptr_t)GetStringUTFLength;
	*(uintptr_t *)(fake_env + 0x2A4) = (uintptr_t)GetStringUTFChars;
	*(uintptr_t *)(fake_env + 0x2A8) = (uintptr_t)ret0; // ReleaseStringUTFChars
	*(uintptr_t *)(fake_env + 0x2AC) = (uintptr_t)GetArrayLength;
	*(uintptr_t *)(fake_env + 0x2B4) = (uintptr_t)GetObjectArrayElement;
	*(uintptr_t *)(fake_env + 0x35C) = (uintptr_t)ret0; // RegisterNatives
	*(uintptr_t *)(fake_env + 0x36C) = (uintptr_t)GetJavaVM;
	*(uintptr_t *)(fake_env + 0x374) = (uintptr_t)GetStringUTFRegion;

	pthread_t t;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 512 * 1024);
	pthread_create(&t, &attr, pthread_main, NULL);

	return sceKernelExitDeleteThread(0);
}
