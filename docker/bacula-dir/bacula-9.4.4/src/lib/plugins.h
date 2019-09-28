/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2016 Kern Sibbald

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
 * Common plugin definitions
 *
 * Kern Sibbald, October 2007
 */
#ifndef __PLUGINS_H
#define __PLUGINS_H

/****************************************************************************
 *                                                                          *
 *                Common definitions for all plugins                        *
 *                                                                          *
 ****************************************************************************/

#ifndef BUILD_PLUGIN
extern DLL_IMP_EXP  alist *b_plugin_list;
#endif

/* Universal return codes from all plugin functions */
typedef enum {
  bRC_OK     = 0,                        /* OK */
  bRC_Stop   = 1,                        /* Stop calling other plugins */
  bRC_Error  = 2,                        /* Some kind of error */
  bRC_More   = 3,                        /* More files to backup */
  bRC_Term   = 4,                        /* Unload me */
  bRC_Seen   = 5,                        /* Return code from checkFiles */
  bRC_Core   = 6,                        /* Let Bacula core handles this file */
  bRC_Skip   = 7,                        /* Skip the proposed file */
  bRC_Cancel = 8,                        /* Job cancelled */

  bRC_Max    = 9999                      /* Max code Bacula can use */
} bRC;



/* Context packet as first argument of all functions */
struct bpContext {
  void *bContext;                        /* Bacula private context */
  void *pContext;                        /* Plugin private context */
};

extern "C" {
typedef bRC (*t_loadPlugin)(void *binfo, void *bfuncs, void **pinfo, void **pfuncs);
typedef bRC (*t_unloadPlugin)(void);
}

class Plugin {
public:
   char *file;
   int32_t file_len;
   t_unloadPlugin unloadPlugin;
   void *pinfo;
   void *pfuncs;
   void *pHandle;
   bool disabled;
   bool restoreFileStarted;
   bool createFileCalled;
};

/* Functions */
extern Plugin *new_plugin();
extern bool load_plugins(void *binfo, void *bfuncs, const char *plugin_dir,
        const char *type, bool is_plugin_compatible(Plugin *plugin));
extern void unload_plugins();

/* Each daemon can register a debug hook that will be called
 * after a fatal signal
 */
typedef void (dbg_plugin_hook_t)(Plugin *plug, FILE *fp);
extern void dbg_plugin_add_hook(dbg_plugin_hook_t *fct);

#endif /* __PLUGINS_H */
