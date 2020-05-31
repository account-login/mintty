#pragma once


typedef struct regex_api_s {
  int inited;
  void *(*reg_new)(const char *pattern);
  int (*reg_exec)(void *prog, const char *string, int *so, int *eo);
  void (*reg_free)(void *prog);
} regex_api_t;

int regex_load_pcre1(regex_api_t *reg_api);
