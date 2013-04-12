#ifndef _CLI_H
#define _CLI_H

#include <Eina.h>
#include "filter.h"
#include "filters.h"

#define BENCHMARK_INIT 0
#define BENCHMARK_SINGLE_FORCED 1
#define BENCHMARK_SINGLE_GRANULARITY 2
#define BENCHMARK_PROCESSING 3

#define BENCHMARK_DELAY_SCALED 0
#define BENCHMARK_DELAY_FULL 1

typedef struct {
  int x, y;  //position
  float scale; //-1 == fit
  Filter_Core *new_f_core; //append filter to graph
  char *setting_float; //change this setting ...
  char *setting_int; //change this setting ...
  int filter; //...of this filter in the graph...
  void *val;  //... to this value
} Bench_Step;

int parse_cli(int argc, char **argv, Eina_List **filters, Bench_Step **bench, int *size, int *metric, int *strategy, char **file, char **dir, int *winsize, int *verbose, int *help);
void print_init_info(Bench_Step *bench, int size, int metric, int strategy, char *file, char *dir);
void bench_time_mark(int type);
void bench_delay_start(void);
void bench_delay_stop(int type);
void bench_report(void);
void bench_start(void);
double bench_delay_get(void);

#endif