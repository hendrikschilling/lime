/*
 * Copyright (C) 2014 Hendrik Siedelmann <hendrik.siedelmann@googlemail.com>
 *
 * This file is part of lime.
 * 
 * Lime is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Lime is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with Lime.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h> 
#include <unistd.h>
#include <string.h>

#include "cli.h"
#include "Lime.h"

#include "filter_convert.h"
#include "filter_gauss.h"
#include "filter_contrast.h"
#include "filter_downscale.h"
#include "filter_memsink.h"
#include "filter_loadtiff.h"
#include "filter_load.h"
#include "filter_savetiff.h"
#include "filter_comparator.h"
#include "filter_sharpen.h"
#include "filter_denoise.h"
#include "filter_pretend.h"
#include "filter_crop.h"
#include "filter_simplerotate.h"
#include "filter_interleave.h"
#include "filter_savejpeg.h"

struct timespec bench_last_mark;
struct timespec t_bench_start;
double bench_times[3];
double max_delay[2] = {0.0, 0.0};
struct timespec bench_delay_mark;


char *strategies[] = {"rand", "rapx", "prob", NULL};
char *metrics[] = {"lru", "dist", "time", "hits", NULL};
char *benchmarks[] = {"global", "pan", "evaluate", "redo", "s0", "s1", "s2", "s3", NULL};
char *nosubopt[] = {NULL};
struct option long_options[] =
{
  {"bench",          optional_argument, 0, 'b'},
  {"window-size",    required_argument, 0, 'w'},
  {"cache-size",     required_argument, 0, 's'},
  {"cache-metric",   required_argument, 0, 'm'},
  {"cache-strategy", required_argument, 0, 'f'},
  {"help",           no_argument,       0, 'h'},
  {"verbose",        no_argument,       0, 'v'},
  {0, 0, 0, 0}
  };
  
float f15 = 1.5;
float f30 = 3.0;
float f45 = 4.5;
float f60 = 6.0;
float f75 = 7.5;
float f90 = 9.0;
float f105 = 10.5;
float f120 = 12.0;
float f02 = 0.4;
float f04 = 0.4;
float f06 = 0.6;
float f08 = 0.8;
float f10 = 0.8;
  
Bench_Step bench_pan[] = {{0,0,-1, &filter_core_gauss}, {0,300,12}, {0,500,8}, {600,1600,4}, {1700,3500,2}, {4000,8000,1}, {4000,8500,1}, {4000,9000,1}, {4000,9500,1}, {4000,10000,1}, {0,0,-1}, {0,250,14}, {500,500,8}, {1800,1800,4}, {4500,4500,2}, {9700,10000,1}, {9700,10500,1}, {9700,11000,1}, {9700,11500,1}, {0,0,-1}, {-1}};

Bench_Step bench_global[] = {{0,0,-1, &filter_core_gauss}, {0,0,-1, NULL, "sigma", NULL, 1, &f15}, {0,0,-1, NULL, "sigma", NULL, 1, &f120}, {0,0,-1, NULL, "sigma", NULL, 1, &f30}, {0,0,-1, NULL, "sigma", NULL, 1, &f105}, {0,0,-1, NULL, "sigma", NULL, 1, &f45}, {0,0,-1, NULL, "sigma", NULL, 1, &f90}, {0,0,-1, NULL, "sigma", NULL, 1, &f60}, {0,0,-1, NULL, "sigma", NULL, 1, &f75}, {-1}};

Bench_Step bench_eval[] = {{0,0,-1, &filter_core_gauss}, {0,300,12}, {0,500,8}, {600,1600,4}, {1700,3500,2}, {4000,8000,1}, {4000,8000,1, NULL, "sigma", NULL, 1, &f15}, {4000,8000,1, NULL, "sigma", NULL, 1, &f30}, {4000,8000,1, NULL, "sigma", NULL, 1, &f45}, {4000,8500,1}, {4000,9000,1}, {4000,9500,1}, {4000,10000,1}, {4000,10000,1, NULL, "sigma", NULL, 1, &f15}, {4000,10000,1, NULL, "sigma", NULL, 1, &f30}, {4000,10000,1, NULL, "sigma", NULL, 1, &f60}, {0,0,-1}, {0,250,14}, {500,500,8}, {1800,1800,4}, {4500,4500,2}, {9700,10000,1}, {9700,10000,1, NULL, "sigma", NULL, 1, &f15}, {9700,10000,1, NULL, "sigma", NULL, 1, &f30}, {9700,10000,1, NULL, "sigma", NULL, 1, &f60}, {9700,10500,1}, {9700,11000,1}, {9700,11500,1}, {9700,11500,1, NULL, "sigma", NULL, 1, &f15}, {9700,11500,1, NULL, "sigma", NULL, 1, &f30}, {9700,11500,1, NULL, "sigma", NULL, 1, &f60}, {0,0,-1}, {-1}};

Bench_Step bench_redo[] = {{0,0,-1, &filter_core_sharpen}, {0,300,12}, {0,500,8}, {600,1600,4}, {1700,3500,2}, {4000,8000,1, NULL, "strength", NULL, 1, &f02}, {4000,8000,1, NULL, "strength", NULL, 1, &f04}, {4000,8000,1, NULL, "strength", NULL, 1, &f06}, {4000,8000,1, NULL, "strength", NULL, 1, &f10}, {0,0,-1, &filter_core_exposure}, {0,300,12}, {0,500,8}, {600,1600,4}, {1700,3500,2}, {4000,8000,1}, {4000,8000,1, NULL, "strength", NULL, 2, &f02}, {4000,8000,1, NULL, "strength", NULL, 2, &f04}, {4000,8000,1, NULL, "strength", NULL, 2, &f06}, {4000,8000,1, NULL, "strength", NULL, 2, &f08}, {-1}};

/*Bench_Step bench_s0[] = {{4096,4096,1}, {-1}};
Bench_Step bench_s1[] = {{2048,2048,2}, {-1}};
Bench_Step bench_s2[] = {{1024,1024,4}, {-1}};
Bench_Step bench_s3[] = {{512,512,8}, {-1}};*/


Bench_Step bench_s0[] = {{2048,2048,1}, {-1}};
Bench_Step bench_s1[] = {{2048,2048,2}, {-1}};
Bench_Step bench_s2[] = {{1024,1024,4}, {-1}};
Bench_Step bench_s3[] = {{512,512,8}, {-1}};

void bench_time_mark(int type)
{
  struct timespec mark;
  clock_gettime(CLOCK_REALTIME, &mark);
  
  bench_times[type] += (mark.tv_sec - bench_last_mark.tv_sec) + (mark.tv_nsec - bench_last_mark.tv_nsec)*0.000000001;
  
}

void bench_delay_start()
{
  clock_gettime(CLOCK_REALTIME, &bench_delay_mark);
}

void bench_delay_stop(int type)
{
  double delay;
  
  struct timespec mark;
  clock_gettime(CLOCK_REALTIME, &mark);
  
  delay = (mark.tv_sec - bench_delay_mark.tv_sec)*1000 + (mark.tv_nsec - bench_delay_mark.tv_nsec)*0.000001; 
  
  if (delay > max_delay[type])
    max_delay[type] = delay;
}

double bench_delay_get(void)
{
  double delay;
  
  struct timespec mark;
  clock_gettime(CLOCK_REALTIME, &mark);
  
  delay = (mark.tv_sec - bench_delay_mark.tv_sec)*1000 + (mark.tv_nsec - bench_delay_mark.tv_nsec)*0.000001; 
  
  return delay;
}

void bench_start(void)
{
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID , &t_bench_start);
}

void bench_report(void)
{
  struct timespec t_bench_stop;
  
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID , &t_bench_stop);
  
  printf("[BENCHMARK] processing took cpu time of %.2fs\n", (t_bench_stop.tv_sec) + (t_bench_stop.tv_nsec)*0.000000001);
  printf("            maximum delay until scaled reaction:  %.0fms\n", max_delay[0]);
  printf("            maximum delay until full res result:  %.0fms\n", max_delay[1]);
}

void bench_copy(Bench_Step *to, Bench_Step *from)
{
  while (from->x != -1) {
    *to = *from;
    to++;
    from++;
  }
  
  to->x = -1;
}

void bench_append(Bench_Step *to, Bench_Step *from)
{
  while (to->x != -1)
    to++;

  bench_copy(to, from);
}

char *clean_dirpath(char *path)
{
  int len;
  path = eina_file_path_sanitize(path);
  
  len = strlen(path);
  if (path[len-1] != '/') {
    path = realloc(path, len+2);
    path[len] = '/';
    path[len+1] = '\0';
  }
  
  return path;
}

int parse_cli(int argc, char **argv, Eina_List **filters, Bench_Step **bench, int *size, int *metric, int *strategy, char **file, char **dir, int *winsize,  int *verbose, int *help)
{
  int i;
  int c;
  int option_index = 0;
  char *subopts, *value;
  char *remain;
  struct stat statbuf;
  Eina_List *new_filters = NULL;
  
  if (bench)
    *bench = 0;
  *size = 25;
  *metric = 0;
  *strategy = CACHE_F_RAPX;
  *verbose = 0;
  if (winsize)
    *winsize = 0;
  if (help)
    *help = 0;
  
  if (file)
    *file = NULL;
  if (dir)
    *dir = NULL;
  
  while ((c = getopt_long(argc, argv, "b:s:m:f:w:vh", long_options, &option_index)) != -1) {
    switch (c) {
      case 'b' :
	if (!bench) {
	  printf("ERROR parsing command line: benchmark is not supported!\n");
	  return -1;
	}
	if (!optarg) {
	  //*bench = create_bench_all();
	}
	else {
	  subopts = optarg;
	  while (*subopts != '\0') {
	    switch(getsubopt(&subopts, benchmarks, &value))
	    {
	      case 0:
		*bench = bench_global;
		break;
	      case 1:
		*bench = bench_pan;
		break;
	      case 2:
		*bench = bench_eval;
		break;
	      case 3:
		*bench = bench_redo;
		break;
	     case 4:
		*bench = bench_s0;
		break;
	      case 5:
		*bench = bench_s1;
		break;
	      case 6:
		*bench = bench_s2;
		break;
	      case 7:
		*bench = bench_s3;
		break;
	      default:
		printf("ERROR parsing command line: unknown benchmark \"%s\"\n", value);
		return -1;
	    }
	  }
	}
	break;
      case 's' :
	*size = atoi(optarg);
	if (*size < 5) {
	  printf("ERROR parsing command line: require cache-size >= 5MB (was %s)\n", optarg);
	  return -1;
	}
	break;
      case 'w' :
	if (winsize) {
	  *winsize = atoi(optarg);
	  if (*winsize == 0) {
	    printf("ERROR parsing command line: require window-size > 0! (was %s)\n", optarg);
	    return -1;
	  }
	}
	else {
	  printf("ERROR winsize not available!\n");
	  return -1;
	}
      case 'h' :
	if (help)
    *help = 1;
	else {
	  printf("ERROR help not available!\n");
	  return 0;
	}
	break;
      case 'v' :
	(*verbose)++;
	break;
      case 'm' :
	subopts = optarg;
	while (*subopts != '\0') {
	  switch(getsubopt(&subopts, strategies, &value))
	  {
	    case 0:
	      *metric = CACHE_F_RAND;
	      break;
	    case 1:
	      *metric = CACHE_F_RAPX;
	      break;
	    case 2:
	      *metric = CACHE_F_PROB;
	      break;
	    default:
	      printf("ERROR parsing command line: unknown strategy \"%s\"\n", value);
	      return -1;
	  }
	}
	break;
      case 'f' :
	subopts = optarg;
	while (*subopts != '\0') {
	  switch(getsubopt(&subopts, metrics, &value))
	  {
	    case 0:
	      *metric |= CACHE_M_LRU;
	      break;
	    case 1:
	      *metric |= CACHE_M_DIST;
	      break;
	    case 2:
	      *metric |= CACHE_M_TIME;
	      break;
	    case 3:
	      *metric |= CACHE_M_HITN;
	      break;
	    default:
	      printf("ERROR parsing command line: unknown metric \"%s\"\n", value);
	      return 2;
	  }
	}
	break;
      default : 
	printf("Unkown option %c!\n", c);
	return -1;
    }
  }
  
  if (!*metric)
    *metric = CACHE_M_LRU;
  
  while (optind < argc) {
    assert(filters);
    new_filters = lime_filter_chain_deserialize(argv[optind]);
    if (*filters && new_filters)
      printf("FIXME append multiple filter chains!\n");
    if (new_filters)
      *filters = new_filters;
    else {
      remain = eina_file_path_sanitize(argv[optind]);
      if (stat(remain, &statbuf)) {
        printf("ERROR parsing command line: %s is not a filter and %s does not exist\n", argv[optind], remain);
        return -1;
      }
      if (S_ISDIR(statbuf.st_mode)) {
        if (!dir) {
          printf("ERROR parsing command line: %s: directory as argument not supported!\n", remain);
          return -1;
        }
        *dir = clean_dirpath(argv[optind]);
      }
      else if (S_ISREG(statbuf.st_mode)) {
        if (!file) {
          printf("ERROR parsing command line: %s: file as argument not supported!\n", remain);
          return -1;
        }
        *file = remain;
        if (dir) {
          *dir = strdup(remain);
          for(i=strlen(remain)-1;i>0;i--)
            if ((*dir)[i] != '/')
              (*dir)[i] = '\0';
            else
              break;
        }
      }
      else
          printf("ERROR parsing command line: %s could not be parsed and %s is neither a regular file nor a directory\n", argv[optind], remain);
    }
    optind++;
  }

  return 0;
}

void print_init_info(Bench_Step *bench, int size, int metric, int strategy, char *file, char *dir)
{
  printf("[INIT] CACHE size %dMB metric %d strategy %d\n", size, metric, strategy);
  if (file)
    printf("[INIT] file: %s\n", file);
  if (dir)
    printf("[INIT] dir: %s\n", dir);
}
