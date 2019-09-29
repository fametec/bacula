/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2018 Kern Sibbald

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
 *    Plugin load/unloader for all Bacula daemons
 *
 * Kern Sibbald, October 2007
 */

#include "bacula.h"
#include <dlfcn.h>
#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif
int breaddir(DIR *dirp, POOLMEM *&d_name);

#ifndef RTLD_NOW
#define RTLD_NOW 2
#endif

#include "plugins.h"

static const int dbglvl = 50;

/*
 * List of all loaded plugins.
 *
 * NOTE!!! This is a global do not try walking it with
 *   foreach_alist, you must use foreach_alist_index !!!!!!
 */
alist *b_plugin_list = NULL;

/*
 * Create a new plugin "class" entry and enter it in the
 *  list of plugins.  Note, this is not the same as
 *  an instance of the plugin.
 */
Plugin *new_plugin()
{
   Plugin *plugin;

   plugin = (Plugin *)malloc(sizeof(Plugin));
   memset(plugin, 0, sizeof(Plugin));
   return plugin;
}

static void close_plugin(Plugin *plugin)
{
   if (plugin->file) {
      Dmsg1(50, "Got plugin=%s but not accepted.\n", plugin->file);
   }
   if (plugin->unloadPlugin) {
      plugin->unloadPlugin();
   }
   if (plugin->pHandle) {
      dlclose(plugin->pHandle);
   }
   if (plugin->file) {
      free(plugin->file);
   }
   free(plugin);
}

/*
 * Load all the plugins in the specified directory.
 */
bool load_plugins(void *binfo, void *bfuncs, const char *plugin_dir,
        const char *type, bool is_plugin_compatible(Plugin *plugin))
{
   bool found = false;
   t_loadPlugin loadPlugin;
   Plugin *plugin = NULL;
   DIR* dp = NULL;
   int name_max;
   struct stat statp;
   POOL_MEM fname(PM_FNAME);
   POOL_MEM dname(PM_FNAME);
   bool need_slash = false;
   int len, type_len;


   Dmsg0(dbglvl, "load_plugins\n");
   name_max = pathconf(".", _PC_NAME_MAX);
   if (name_max < 1024) {
      name_max = 1024;
   }

   if (!(dp = opendir(plugin_dir))) {
      berrno be;
      Jmsg(NULL, M_ERROR_TERM, 0, _("Failed to open Plugin directory %s: ERR=%s\n"),
            plugin_dir, be.bstrerror());
      Dmsg2(dbglvl, "Failed to open Plugin directory %s: ERR=%s\n",
            plugin_dir, be.bstrerror());
      goto get_out;
   }

   len = strlen(plugin_dir);
   if (len > 0) {
      need_slash = !IsPathSeparator(plugin_dir[len - 1]);
   }
   for ( ;; ) {
      plugin = NULL;            /* Start from a fresh plugin */

      if ((breaddir(dp, dname.addr()) != 0)) {
         if (!found) {
            Dmsg1(dbglvl, "Failed to find any plugins in %s\n", plugin_dir);
         }
         break;
      }
      if (strcmp(dname.c_str(), ".") == 0 ||
          strcmp(dname.c_str(), "..") == 0) {
         continue;
      }

      len = strlen(dname.c_str());
      type_len = strlen(type);
      if (len < type_len+1 || strcmp(&dname.c_str()[len-type_len], type) != 0) {
         Dmsg3(dbglvl, "Rejected plugin: want=%s name=%s len=%d\n", type, dname.c_str(), len);
         continue;
      }
      Dmsg2(dbglvl, "Found plugin: name=%s len=%d\n", dname.c_str(), len);

      pm_strcpy(fname, plugin_dir);
      if (need_slash) {
         pm_strcat(fname, "/");
      }
      pm_strcat(fname, dname);
      if (lstat(fname.c_str(), &statp) != 0 || !S_ISREG(statp.st_mode)) {
         continue;                 /* ignore directories & special files */
      }

      plugin = new_plugin();
      plugin->file = bstrdup(dname.c_str());
      plugin->file_len = strstr(plugin->file, type) - plugin->file;
      plugin->pHandle = dlopen(fname.c_str(), RTLD_NOW);
      if (!plugin->pHandle) {
         const char *error = dlerror();
         Jmsg(NULL, M_ERROR, 0, _("dlopen plugin %s failed: ERR=%s\n"),
              fname.c_str(), NPRT(error));
         Dmsg2(dbglvl, "dlopen plugin %s failed: ERR=%s\n", fname.c_str(),
               NPRT(error));
         close_plugin(plugin);
         continue;
      }

      /* Get two global entry points */
      loadPlugin = (t_loadPlugin)dlsym(plugin->pHandle, "loadPlugin");
      if (!loadPlugin) {
         Jmsg(NULL, M_ERROR, 0, _("Lookup of loadPlugin in plugin %s failed: ERR=%s\n"),
            fname.c_str(), NPRT(dlerror()));
         Dmsg2(dbglvl, "Lookup of loadPlugin in plugin %s failed: ERR=%s\n",
            fname.c_str(), NPRT(dlerror()));
         close_plugin(plugin);
         continue;
      }
      plugin->unloadPlugin = (t_unloadPlugin)dlsym(plugin->pHandle, "unloadPlugin");
      if (!plugin->unloadPlugin) {
         Jmsg(NULL, M_ERROR, 0, _("Lookup of unloadPlugin in plugin %s failed: ERR=%s\n"),
            fname.c_str(), NPRT(dlerror()));
         Dmsg2(dbglvl, "Lookup of unloadPlugin in plugin %s failed: ERR=%s\n",
            fname.c_str(), NPRT(dlerror()));
         close_plugin(plugin);
         continue;
      }

      /* Initialize the plugin */
      if (loadPlugin(binfo, bfuncs, &plugin->pinfo, &plugin->pfuncs) != bRC_OK) {
         close_plugin(plugin);
         continue;
      }
      if (!is_plugin_compatible) {
         Dmsg0(50, "Plugin compatibility pointer not set.\n");
      } else if (!is_plugin_compatible(plugin)) {
         close_plugin(plugin);
         continue;
      }

      found = true;                /* found a plugin */
      b_plugin_list->append(plugin);
   }

get_out:
   if (!found && plugin) {
      close_plugin(plugin);
   }
   if (dp) {
      closedir(dp);
   }
   return found;
}

/*
 * Unload all the loaded plugins
 */
void unload_plugins()
{
   Plugin *plugin;

   if (!b_plugin_list) {
      return;
   }
   foreach_alist(plugin, b_plugin_list) {
      /* Shut it down and unload it */
      plugin->unloadPlugin();
      dlclose(plugin->pHandle);
      if (plugin->file) {
         free(plugin->file);
      }
      free(plugin);
   }
   delete b_plugin_list;
   b_plugin_list = NULL;
}

/*
 * Dump plugin information
 * Each daemon can register a hook that will be called
 * after a fatal signal.
 */
#define DBG_MAX_HOOK 10
static dbg_plugin_hook_t *dbg_plugin_hooks[DBG_MAX_HOOK];
static int dbg_plugin_hook_count=0;

void dbg_plugin_add_hook(dbg_plugin_hook_t *fct)
{
   ASSERT(dbg_plugin_hook_count < DBG_MAX_HOOK);
   dbg_plugin_hooks[dbg_plugin_hook_count++] = fct;
}

void dbg_print_plugin(FILE *fp)
{
   Plugin *plugin;
   fprintf(fp, "List plugins. Hook count=%d\n", dbg_plugin_hook_count);

   if (!b_plugin_list) {
      return;
   }
   foreach_alist(plugin, b_plugin_list) {
      for(int i=0; i < dbg_plugin_hook_count; i++) {
//       dbg_plugin_hook_t *fct = dbg_plugin_hooks[i];
         fprintf(fp, "Plugin %p name=\"%s\" disabled=%d\n",
                 plugin, plugin->file, plugin->disabled);
//       fct(plugin, fp);
      }
   }
}
