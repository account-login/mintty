#include "term.h"
#include "charset.h"
#include "regsearch.h"

#include <sys/types.h>
#include <regex.h>
#include <stringapiset.h>   // for WideCharToMultiByte


static int
wchar2utf8(const wchar_t *wc, int wcsize, char *out)
{
  assert(wcsize == 1 || wcsize == 2);
  return WideCharToMultiByte(CP_UTF8, 0, wc, wcsize, out, 4, 0, 0);
}

static void
extract_utf8(int begin, int end, char **pdata, int **off2idx, int *psize)
{
  int cap = 4 * (end - begin);
  *pdata = malloc(cap + 1);                     // nul terminated
  *off2idx = malloc(sizeof(int) * (cap + 1));   // cap + 1 to allow off by one lookup
  int size = 0;

  // Loop over every code point.
  termline * line = NULL;
  int line_y = -1;
  for (int idx = begin; idx < end; ++idx) {
    // Determine the current position.
    int x = (idx % term.cols);
    int y = (idx / term.cols);
    if (line_y != y) {
      // If our current position isn't in the termline, add it in.
      if (line) {
        release_line(line);
      }
      line = fetch_line(y - term.sblines);
      line_y = y;
    }

    // The current code point
    int wcsize = 1;
    wchar wc[2];
    termchar * chr = line->chars + x;
    wc[0] = chr->chr;
    if (is_high_surrogate(chr->chr)) {
      if (!chr->cc_next) {
        continue;
      }
      termchar * cc = chr + chr->cc_next;
      if (!is_low_surrogate(cc->chr)) {
        continue;
      }
      wc[1] = cc->chr;
      wcsize = 2;
    }
    // Skip the second cell of any wide characters
    if (wc[0] == UCSWIDE) {
      continue;
    }

    // encode
    int code_len = wchar2utf8(wc, wcsize, *pdata + size);
    assert(0 <= code_len && code_len <= 4);
    for (int i = size; i < size + code_len; ++i) {
      (*off2idx)[i] = idx;
    }
    size += code_len;
  }
  assert(size <= cap);
  *psize = size;
  (*pdata)[size] = 0;         // nul termicated
  (*off2idx)[size] = end;     // allow off by one lookup

  // Clean up
  if (line) {
      release_line(line);
  }
}

static void *
posix_reg_new(const char *pattern)
{
  regex_t *prog = malloc(sizeof(regex_t));
  int err = regcomp(prog, pattern, REG_EXTENDED | REG_ICASE);
  if (err) {
    free(prog);
    prog = NULL;
  }
  return prog;
}

static int
posix_reg_exec(void *prog, const char *string, int *so, int *eo) {
  string += *so;
  regmatch_t match;
  int eflag = REG_NOTBOL | REG_NOTEOL;    // ^ and $ does not make sense
#ifdef REG_STARTEND
    eflag |= REG_STARTEND;
    match.rm_so = 0;
    match.rm_eo = *eo - *so;
#endif
  int err = regexec((const regex_t *)prog, string, 1, &match, eflag);
  if (!err) {
    *so += match.rm_so;
    *eo = *so + match.rm_eo - match.rm_so;
  }
  return err;
}

static void
posix_reg_free(void *prog)
{
  regfree((regex_t *)prog);
  free(prog);
}

static regex_api_t reg_api = {
  .inited = 0,
  .reg_new = &posix_reg_new,
  .reg_exec = &posix_reg_exec,
  .reg_free = &posix_reg_free,
};

void
term_search_regex(int begin, int end)
{
  if (begin == end) {
    return;
  }

  // init regex api
  if (!reg_api.inited) {
    (void) regex_load_pcre1(&reg_api);
    reg_api.inited = 1;
  }

  // compile regex
  char *pattern = cs__wcstoutf(term.results.query);
  void *reg = reg_api.reg_new(pattern);
  free(pattern);
  if (!reg) {
    // bad regex
    return;
  }

  // extract contents
  char *data = NULL;
  int *off2idx = NULL;
  int size = 0;
  extract_utf8(begin, end, &data, &off2idx, &size);

  // exec regex
  for (int start = 0; start < size; ) {
    int so = start;
    int eo = size;
    if (0 != reg_api.reg_exec(reg, data, &so, &eo)) {
      break;
    }
    assert(start <= so && so <= eo);

    // Append result
    result run = {
      .idx = off2idx[so],
      .len = off2idx[eo] - off2idx[so],
    };
    assert(begin <= run.idx && (run.idx + run.len) <= end);
    if (run.len > 0) {
      term_search_results_add(run);
      start = eo;
    } else {
      // zero length match?
      start += 1;
    }
  }

  // clean up
  free(off2idx);
  free(data);
  reg_api.reg_free(reg);
}
