#ifndef _CONFIGURATION_H
#define _CONFIGURATION_H

#include "filter.h"

//TODO split public (test...) private (lime_configuration_reset)

int lime_config_test(Filter *f);

void lime_config_reset(void);
void lime_config_node_add(Fg_Node *node);
void lime_config_node_del(Fg_Node *node);

#endif