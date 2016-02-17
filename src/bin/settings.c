#include "settings.h"

#include <Eet.h>

#include <assert.h>

static Eet_Data_Descriptor *lv_settings_desc;
static Eet_Data_Descriptor *lv_fc_rule_desc;

static int inits = 0;

static const char *filename = "/home/hendrik/.config/limeviewrc.eet";

void lv_settings_init(void)
{
  inits++;
  if (inits > 1)
    return;
  
  
  Eet_Data_Descriptor_Class eddc_rule;
  EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc_rule, Default_Fc_Rule);
  lv_fc_rule_desc = eet_data_descriptor_stream_new(&eddc_rule);
  
#define MY_CONF_ADD_BASIC(member, eet_type) \
  EET_DATA_DESCRIPTOR_ADD_BASIC             \
  (lv_fc_rule_desc, Default_Fc_Rule, # member, member, eet_type)
    MY_CONF_ADD_BASIC(cam, EET_T_STRING);
    MY_CONF_ADD_BASIC(format, EET_T_STRING);
    MY_CONF_ADD_BASIC(fc, EET_T_STRING);
#undef MY_CONF_ADD_BASIC
  
  Eet_Data_Descriptor_Class eddc_settings;
  EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc_settings, Limeview_Settings);
  lv_settings_desc = eet_data_descriptor_stream_new(&eddc_settings);
  
#define MY_CONF_ADD_BASIC(member, eet_type) \
  EET_DATA_DESCRIPTOR_ADD_BASIC             \
  (lv_settings_desc, Limeview_Settings, # member, member, eet_type)
    MY_CONF_ADD_BASIC(version, EET_T_UINT);
    MY_CONF_ADD_BASIC(cache_size, EET_T_UINT);
    MY_CONF_ADD_BASIC(high_quality_delay, EET_T_UINT);
#undef MY_CONF_ADD_BASIC
    
  EET_DATA_DESCRIPTOR_ADD_LIST(lv_settings_desc, Limeview_Settings, "default_fc_rules", default_fc_rules,
     lv_fc_rule_desc);
}

void lv_settings_shutdown(void)
{
  assert(inits);
  inits--;
  
  if (inits)
    return;
  
  eet_data_descriptor_free(lv_settings_desc);
  eet_data_descriptor_free(lv_fc_rule_desc);
}



Limeview_Settings *lv_setting_new(void)
{
  Limeview_Settings *s = calloc(sizeof(Limeview_Settings), 1);
  s->version = 1;
  s->cache_size = 100;
  
  return s;
}

void lv_setting_free(Limeview_Settings *s)
{
  free(s);
}

Limeview_Settings *lv_setting_load(void)
{
  Limeview_Settings *s;
  
  Eet_File *ef = eet_open(filename, EET_FILE_MODE_READ);
  if (!ef) {
    printf("configuration in \"%s\" does not exist or not valid eet file\n", filename);
    return lv_setting_new();
  }

  s = eet_data_read(ef, lv_settings_desc, "config");
  if (!s) {
    printf("configuration in \"%s\" is not valid\n", filename);
    eet_close(ef);
    return lv_setting_new();
  }

  assert(s->version == 1);
  
  return s;
}

void lv_setting_save(Limeview_Settings *s)
{
  char tmp[PATH_MAX];
  Eet_File *ef;
  Eina_Bool ret;
  unsigned int i, len;
  struct stat st;
  
  len = eina_strlcpy(tmp, filename, sizeof(tmp));
  
  if (len + 12 >= (int)sizeof(tmp)) {
    fprintf(stderr, "ERROR: file name is too big: %s\n", filename);
    return;
  }

   i = 0;
   do {
        snprintf(tmp + len, 12, ".%u", i);
        i++;
     }
   while (stat(tmp, &st) == 0);


   ef = eet_open(tmp, EET_FILE_MODE_WRITE);
   if (!ef) {
        fprintf(stderr, "ERROR: could not open '%s' for write\n", tmp);
        return;
     }


   ret = eet_data_write(ef, lv_settings_desc, "config", s, EINA_TRUE);
   eet_close(ef);
   
   if (ret) {
     unlink(filename);
     rename(tmp, filename);
   }
   else {
    fprintf(stderr, "ERROR: while writing temporary config to '%s', leaving old config in place\n", tmp);
  }
}
