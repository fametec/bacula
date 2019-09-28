/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2017 Kern Sibbald

   The original author of Bacula is Kern Sibbald, with contributions
   from many others, a complete list can be found in the file AUTHORS.

   You may use this file and others of this release according to the
   license defined in the LICENSE file, which includes the Affero General
   Public License, v3.0 ("AGPLv3") and some additional permissions and
   terms pursuant to its AGPLv3 Section 7.

   This notice must be preserved when any source code is
   conveyed and/or propagated.

   Bacula(R) is a registered trademark of Kern Sibbald.
*/
/*
 * Main program to test loading and running Bacula plugins.
 *   Destined to become Bacula pluginloader, ...
 *
 * Kern Sibbald, October 2007
 */
#include "bacula.h"
#include <dlfcn.h>
#include "lib/plugin.h"
#include "plugin-sd.h"

const char *plugin_type = "-sd.so";


/* Forward referenced functions */
static bpError baculaGetValue(bpContext *ctx, bVariable var, void *value);
static bpError baculaSetValue(bpContext *ctx, bVariable var, void *value);


/* Bacula entry points */
static bFuncs bfuncs = {
   sizeof(bFuncs),
   PLUGIN_INTERFACE,
   baculaGetValue,
   baculaSetValue,
   NULL,
   NULL
};
    



int main(int argc, char *argv[])
{
   char plugin_dir[1000];
   bpContext ctx;
   bEvent event;
   Plugin *plugin;
    
   b_plugin_list = New(alist(10, not_owned_by_alist));

   ctx.bContext = NULL;
   ctx.pContext = NULL;
   getcwd(plugin_dir, sizeof(plugin_dir)-1);

   load_plugins((void *)&bfuncs, plugin_dir, plugin_type);

   foreach_alist(plugin, b_plugin_list) {
      printf("bacula: plugin_size=%d plugin_version=%d\n", 
              pref(plugin)->size, pref(plugin)->interface);
      printf("License: %s\nAuthor: %s\nDate: %s\nVersion: %s\nDescription: %s\n",
         pref(plugin)->plugin_license, pref(plugin)->plugin_author, 
         pref(plugin)->plugin_date, pref(plugin)->plugin_version, 
         pref(plugin)->plugin_description);

      /* Start a new instance of the plugin */
      pref(plugin)->newPlugin(&ctx);
      event.eventType = bEventNewVolume;   
      pref(plugin)->handlePluginEvent(&ctx, &event);
      /* Free the plugin instance */
      pref(plugin)->freePlugin(&ctx);

      /* Start a new instance of the plugin */
      pref(plugin)->newPlugin(&ctx);
      event.eventType = bEventNewVolume;   
      pref(plugin)->handlePluginEvent(&ctx, &event);
      /* Free the plugin instance */
      pref(plugin)->freePlugin(&ctx);
   }

   unload_plugins();

   printf("bacula: OK ...\n");
   close_memory_pool();
   sm_dump(false);
   return 0;
}

static bpError baculaGetValue(bpContext *ctx, bVariable var, void *value)
{
   printf("bacula: baculaGetValue var=%d\n", var);
   if (value) {
      *((int *)value) = 100;
   }
   return 0;
}

static bpError baculaSetValue(bpContext *ctx, bVariable var, void *value)
{
   printf("bacula: baculaSetValue var=%d\n", var);
   return 0;
}
