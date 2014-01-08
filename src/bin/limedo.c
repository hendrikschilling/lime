#include "Lime.h"
#include "cli.h"

void print_help(void)
{
  printf("usage: limedo - execute filter chain\n");
  printf("   limedo [options] [filter1[:set1=val1[:set2=val2]]][,filter2] ... inputfile\n");
  printf("filter may be one of:\n   \"gauss\", \"sharpen\", \"denoise\", \"contrast\", \"exposure\", \"convert\", \"assert\"\n");
  printf("source filter is set by the application\nsink filter is the last filter in the chain and has to be set to either\n   \"\"savetiff\" or \"compare\", e.g. \"savetiff=0,blablub.tif\" to save to blablub.tif in sRGB colorspace,\n   or \"compare=1\" to compare scales in LAB color space\n");
  printf("options:\n");
  printf("   --help,           -h  show this help\n");
  printf("   --cache-size,     -s  set cache size in megabytes (default: 100)\n");
  printf("   --cache-metric,   -m  set cache cache metric (lru/dist/time/hits), \n                         can be repeated for a combined metric (default: lru)\n");
  printf("   --cache-strategy, -f  set cache strategy (rand/rapx/prob, default rapx)\n");
  printf("   --verbose,        -v  prints some more information, mainly cache usage statistics\n");
}

int
main(int argc, char **argv)
{
  int cache_strategy, cache_metric, cache_size, help;
  Eina_List *filters = NULL,
	    *list_iter;
  Filter *f, *last, *load, *sink; 
  char *file = NULL;
  int verbose;
  
  lime_init();

  if (parse_cli(argc, argv, &filters, NULL, &cache_size, &cache_metric, &cache_strategy, &file, NULL, NULL, &verbose, &help))
    return EXIT_FAILURE;
  
  if (help) {
    print_help();
    return EXIT_SUCCESS;
  }
  
  print_init_info(NULL, cache_size, cache_metric, cache_strategy, NULL, NULL);
  
  lime_cache_set(cache_size, cache_strategy | cache_metric);
  
  if (!file) { 
    printf("ERROR: need file to execute filter chain!\n");
    return EXIT_FAILURE;
  }
  load = lime_filter_new("load");
  lime_setting_string_set(load, "filename", file);
  filters = eina_list_prepend(filters, load);
  
  last = NULL;
  EINA_LIST_FOREACH(filters, list_iter, f) {
    if (last)
      filter_connect(last, 0, f, 0);
    
    last = f;
  }
  
  sink = last;

  lime_render(sink);
  
  cache_stats_print();
  
  lime_shutdown();
  
  return 0;
}
