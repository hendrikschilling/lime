#ifndef _LIME_FC_H
#define _LIME_FC_H

struct _Lime_Filter_Chain;
typedef struct _Lime_Filter_Chain Lime_Filter_Chain;

typedef struct {
  Lime_Filter_Chain *subchain;
  char *subfile;
} Lime_FC_Element;

#endif