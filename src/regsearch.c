#include "term.h"
#include "charset.h"

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

void
term_search_regex(int begin, int end)
{
  if (begin == end) {
    return;
  }

  // compile regex
  regex_t reg;
  char *pattern = cs__wcstoutf(term.results.query);
  int reg_err = regcomp(&reg, pattern, REG_EXTENDED | REG_ICASE);
  free(pattern);
  if (reg_err) {
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
    regmatch_t match;
    int eflag = REG_NOTBOL | REG_NOTEOL;    // ^ and $ does not make sense
#ifdef REG_STARTEND
    eflag |= REG_STARTEND;
    match.rm_so = 0;
    match.rm_eo = size - start;
#endif
    if (0 != regexec(&reg, &data[start], 1, &match, eflag)) {
      // not matched
      break;
    }
    assert(match.rm_so >= 0 && match.rm_eo >= match.rm_so);

    // Append result
    int idx_begin = off2idx[start + match.rm_so];
    int idx_end = off2idx[start + match.rm_eo];     // off by one is ok
    result run = {
      .idx = idx_begin,
      .len = idx_end - idx_begin,
    };
    assert(begin <= run.idx && (run.idx + run.len) <= end);
    if (run.len > 0) {
      term_search_results_add(run);
      start += match.rm_eo;
    } else {
      // zero length match?
      start += 1;
    }
  }

  // clean up
  free(off2idx);
  free(data);
  regfree(&reg);
}
