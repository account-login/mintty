#include "regsearch.h"

#include <dlfcn.h>
// #include <pcre.h>    // optional


// from <pcre.h>
#define PCRE_CASELESS           0x00000001  /* C1       */
#define PCRE_UTF8               0x00000800  /* C4        )          */
#define PCRE_NOTBOL             0x00000080  /*    E D J */
#define PCRE_NOTEOL             0x00000100  /*    E D J */
#define PCRE_NOTEMPTY           0x00000400  /*    E D J */
#define PCRE_NO_AUTO_CAPTURE    0x00001000  /* C1       */

typedef struct pcre pcre;
typedef struct pcre_extra pcre_extra;
typedef pcre *(*func_pcre_compile)(
  const char *pattern, int options, const char **errptr, int *erroffset,
  const unsigned char *tableptr
);
typedef int (*func_pcre_exec)(
  const pcre *code, const pcre_extra *extra,
  const char *subject, int length, int startoffset, int options,
  int *ovector, int ovecsize
);

static struct {
  func_pcre_compile pcre_compile;
  func_pcre_exec pcre_exec;
  // no pcre_free, since pcre_free is not an exported function in libpcre.
} pcre1_api;


static void *
pcre1_reg_new(const char *pattern)
{
  int options = PCRE_CASELESS | PCRE_UTF8 | PCRE_NO_AUTO_CAPTURE;
  const char *error = "";
  int erroroffset = 0;
  pcre *prog = pcre1_api.pcre_compile(pattern, options, &error, &erroroffset, NULL);
  return prog;
}

static int
pcre1_reg_exec(void *prog, const char *string, int *so, int *eo)
{
  int ovector[3] = {};
  int options = PCRE_NOTBOL | PCRE_NOTEOL | PCRE_NOTEMPTY;
  int rv = pcre1_api.pcre_exec((const pcre *)prog, NULL, string, *eo, *so, options, ovector, sizeof(ovector));
  if (rv >= 0) {
    *so = ovector[0];
    *eo = ovector[1];
    return 0;
  }
  return rv;
}

static void
pcre1_reg_free(void *prog)
{
  free(prog);
}

int
regex_load_pcre1(regex_api_t *reg_api)
{
  // cygpcre-1.dll should be present in most systems since it is a dependency of `grep`
  void *handle = dlopen("/usr/bin/cygpcre-1.dll", 0);
  if (!handle) {
    return -1;
  }
  pcre1_api.pcre_compile = (func_pcre_compile)dlsym(handle, "pcre_compile");
  pcre1_api.pcre_exec = (func_pcre_exec)dlsym(handle, "pcre_exec");
  if (!pcre1_api.pcre_compile || !pcre1_api.pcre_exec) {
    return -1;
  }

  reg_api->reg_new = &pcre1_reg_new;
  reg_api->reg_exec = &pcre1_reg_exec;
  reg_api->reg_free = &pcre1_reg_free;
  return 0;
}
