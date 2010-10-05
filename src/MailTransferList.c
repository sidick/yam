/***************************************************************************

 YAM - Yet Another Mailer
 Copyright (C) 1995-2000 by Marcel Beck <mbeck@yam.ch>
 Copyright (C) 2000-2010 by YAM Open Source Team

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

 YAM Official Support Site :  http://www.yam.ch
 YAM OpenSource project    :  http://sourceforge.net/projects/yamos/

 $Id$

***************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <clib/alib_protos.h>
#include <proto/exec.h>

#include "extrasrc.h"

#include "YAM_mainFolder.h"
#include "MailTransferList.h"

#include "Debug.h"

/// InitMailTransferList
// initialize a transfer list
// NOTE: the embedded semaphore must NOT be used for such a list
void InitMailTransferList(struct MailTransferList *tlist)
{
  ENTER();

  NewMinList(&tlist->list);
  tlist->count = 0;

  LEAVE();
}

///
/// ClearMailTransferList
// removed all nodes from a transfer list
void ClearMailTransferList(struct MailTransferList *tlist)
{
  struct Node *node;

  ENTER();

  while((node = RemHead((struct List *)&tlist->list)) != NULL)
  {
    struct MailTransferNode *tnode = (struct MailTransferNode *)node;

    DeleteMailTransferNode(tnode);
  }
  tlist->count = 0;

  LEAVE();
}

///
/// CreateMailTransferList
// create a new list for transfers
struct MailTransferList *CreateMailTransferList(void)
{
  struct MailTransferList *tlist;

  ENTER();

  // at first create the list itself
  if((tlist = AllocSysObjectTags(ASOT_LIST, ASOLIST_Size, sizeof(*tlist),
                                            ASOLIST_Min,  TRUE,
                                            TAG_DONE)) != NULL)
  {
    // now create the arbitration semaphore
    if((tlist->lockSemaphore = AllocSysObjectTags(ASOT_SEMAPHORE, TAG_DONE)) != NULL)
    {
      // no transfers in the list so far
      tlist->count = 0;
    }
    else
    {
      // free the list again on failure
      FreeSysObject(ASOT_LIST, tlist);
      tlist = NULL;
    }
  }

  RETURN(tlist);
  return tlist;
}

///
/// DeleteMailTransferList
// remove all nodes from a list and free it completely
void DeleteMailTransferList(struct MailTransferList *tlist)
{
  ENTER();

  if(tlist != NULL)
  {
    struct MailTransferNode *tnode;

    // lock the list just, just for safety reasons
    LockMailTransferList(tlist);

    // remove and free all remaining nodes in the list
    while((tnode = (struct MailTransferNode *)RemHead((struct List *)&tlist->list)) != NULL)
      DeleteMailTransferNode(tnode);

    // unlock the list again
    UnlockMailTransferList(tlist);

    // free the semaphore
    FreeSysObject(ASOT_SEMAPHORE, tlist->lockSemaphore);
    tlist->lockSemaphore = NULL;

    // free the list itself
    FreeSysObject(ASOT_LIST, tlist);
  }

  LEAVE();
}

///
/// ScanMailTransferList
// iterate over a transfer list and return TRUE if at least one node
// has the given flags set
BOOL ScanMailTransferList(const struct MailTransferList *tlist, const ULONG flags)
{
  BOOL found = FALSE;
  struct MailTransferNode *tnode;

  ENTER();

  ForEachMailTransferNode(tlist, tnode)
  {
    if(isFlagSet(tnode->tflags, flags))
    {
      found = TRUE;
      break;
    }
  }

  RETURN(found);
  return found;
}

///
/// CreateMailTransferNode
// create a new transfer node, a given mail pointer will be memdup()'ed
struct MailTransferNode *CreateMailTransferNode(const struct Mail *mail, const ULONG flags)
{
  struct MailTransferNode *tnode;

  ENTER();

  if((tnode = AllocSysObjectTags(ASOT_NODE, ASONODE_Size, sizeof(*tnode),
                                            ASONODE_Min, TRUE,
                                            TAG_DONE)) != NULL)
  {
    // clear the structure, ASOT() does not do that for us
    memset(tnode, 0, sizeof(*tnode));

    tnode->tflags = flags;

    if(mail != NULL)
    {
      if((tnode->mail = memdup(mail, sizeof(*mail))) == NULL)
      {
        FreeSysObject(ASOT_NODE, tnode);
        tnode = NULL;
      }
    }
  }

  RETURN(tnode);
  return tnode;
}

///
/// AddMailTransferNode
// add a transfer node to an existing list
// if locking of the list is needed this must be done by the calling function
void AddMailTransferNode(struct MailTransferList *tlist, struct MailTransferNode *tnode)
{
  ENTER();

  // we only accept existing transfers
  if(tlist != NULL && tnode != NULL && tnode->mail != NULL)
  {
    // add the new transfer node to the end of the list
    AddTail((struct List *)&tlist->list, (struct Node *)&tnode->node);

    // and increase the counter
    tlist->count++;
  }

  LEAVE();
}

///
/// RemoveMailTransferNode
// remove a transfer node from the list, the node is NOT freed
// if locking of the list is needed this must be done by the calling function
void RemoveMailTransferNode(struct MailTransferList *tlist, struct MailTransferNode *tnode)
{
  ENTER();

  if(tlist != NULL && tnode != NULL)
  {
    // remove the transfer node from the list
    Remove((struct Node *)&tnode->node);

    // and decrease the counter
    tlist->count--;
  }

  LEAVE();
}

///
/// DeleteMailTransferNode
// free a transfer node that does not belong to a list
void DeleteMailTransferNode(struct MailTransferNode *tnode)
{
  ENTER();

  free(tnode->mail);
  free(tnode->uidl);
  FreeSysObject(ASOT_NODE, tnode);

  LEAVE();
}

///

#if defined(DEBUG)
static LONG transferLocks = 0;

/// LockMailTransferList()
void LockMailTransferList(const struct MailTransferList *tlist)
{
  ENTER();

  if(AttemptSemaphore(tlist->lockSemaphore) == FALSE)
  {
    if(transferLocks > 0)
      E(DBF_ALWAYS, "nested (%ld) exclusive lock of MailTransferList %08lx", transferLocks + 1, tlist);
    ObtainSemaphore(tlist->lockSemaphore);
  }

  transferLocks++;

  LEAVE();
}

///
/// LockMailTransferListShared()
void LockMailTransferListShared(const struct MailTransferList *tlist)
{
  ENTER();

  if(AttemptSemaphoreShared(tlist->lockSemaphore) == FALSE)
  {
    if(transferLocks > 0)
      E(DBF_ALWAYS, "nested (%ld) shared lock of MailTransferList %08lx", transferLocks + 1, tlist);
    ObtainSemaphoreShared(tlist->lockSemaphore);
  }

  transferLocks++;

  LEAVE();
}

///
/// UnlockMailTransferList()
void UnlockMailTransferList(const struct MailTransferList *tlist)
{
  ENTER();

  transferLocks--;
  if(transferLocks < 0)
    E(DBF_ALWAYS, "too many unlocks (%ld) of MailTransferList %08lx", transferLocks, tlist);

  ReleaseSemaphore(tlist->lockSemaphore);

  LEAVE();
}

///
#endif
