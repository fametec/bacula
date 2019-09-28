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
 *  Written by James Harper, July 2010
 *  
 *  Used only in "old Exchange plugin" now deprecated.
 */

#include "exchange-fd.h"

node_t::node_t(char *name, int type)
{
   this->type = type;
   state = 0;
   parent = NULL;
   this->name = bstrdup(name);
   full_path = make_full_path();
   size = 0;
   level = 0;
}

node_t::node_t(char *name, int type, node_t *parent_node)
{
   this->type = type;
   state = 0;
   parent = parent_node;
   this->name = bstrdup(name);
   full_path = make_full_path();
   size = 0;
   level = parent->level + 1;
}

node_t::~node_t()
{
   safe_delete(name);
   safe_delete(full_path);
}

char *
node_t::make_full_path()
{
   node_t *curr_node;
   int len;
   char *retval;

   for (len = 0, curr_node = this; curr_node != NULL; curr_node = curr_node->parent)
   {
      len += strlen(curr_node->name) + 1;
   }
   if (type == NODE_TYPE_FILE || type == NODE_TYPE_DATABASE_INFO)
   {
      retval = new char[len + 1];
      retval[len] = 0;
   }
   else
   {
      retval = new char[len + 2];
      retval[len] = '/';
      retval[len + 1] = 0;
   }
   for (curr_node = this; curr_node != NULL; curr_node = curr_node->parent)
   {
      len -= strlen(curr_node->name);
      memcpy(retval + len, curr_node->name, strlen(curr_node->name));
      retval[--len] = '/';
   }
   return retval;
}

bRC
node_t::pluginIoOpen(exchange_fd_context_t *context, struct io_pkt *io)
{
   _DebugMessage(100, "pluginIoOpen_Node\n");
   io->status = 0;
   io->io_errno = 0;
   return bRC_OK;
}

bRC
node_t::pluginIoRead(exchange_fd_context_t *context, struct io_pkt *io)
{
   _DebugMessage(100, "pluginIoRead_Node\n");
   io->status = 0;
   io->io_errno = 0;
   return bRC_OK;
}

bRC
node_t::pluginIoWrite(exchange_fd_context_t *context, struct io_pkt *io)
{
   _DebugMessage(100, "pluginIoWrite_Node\n");
   io->status = 0;
   io->io_errno = 1;
   return bRC_Error;
}

bRC
node_t::pluginIoClose(exchange_fd_context_t *context, struct io_pkt *io)
{
   _DebugMessage(100, "pluginIoClose_Node\n");
   io->status = 0;
   io->io_errno = 0;
   return bRC_OK;
}
