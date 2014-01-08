/***************************************************************************

 YAM - Yet Another Mailer
 Copyright (C) 1995-2000 Marcel Beck
 Copyright (C) 2000-2014 YAM Open Source Team

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

#include <libraries/mui.h>
#include <proto/exec.h>

#include "extrasrc.h"

#include "YAM.h"
#include "YAM_write.h"

#include "mui/ClassesExtra.h"
#include "mui/WriteWindow.h"

#include "Config.h"
#include "Rexx.h"
#include "UserIdentity.h"

#include "Debug.h"

struct args
{
  char *identity;
  char *address;
};

void rx_writeidentity(UNUSED struct RexxHost *host, struct RexxParams *params, enum RexxAction action, UNUSED struct RexxMsg *rexxmsg)
{
  struct args *args = params->args;

  ENTER();

  switch(action)
  {
    case RXIF_INIT:
    {
      params->args = AllocVecPooled(G->SharedMemPool, sizeof(*args));
    }
    break;

    case RXIF_ACTION:
    {
      if(G->ActiveRexxWMData != NULL && G->ActiveRexxWMData->window != NULL)
      {
        struct UserIdentityNode *uin;

        if(args->identity != NULL)
          uin = FindUserIdentityByDescription(&C->userIdentityList, args->identity);
        else if(args->address != NULL)
          uin = FindUserIdentityByAddress(&C->userIdentityList, args->address);
        else
          uin = NULL;

        if(uin != NULL)
          set(G->ActiveRexxWMData->window, MUIA_WriteWindow_Identity, uin);
        else
          params->rc = RETURN_ERROR;
      }
      else
        params->rc = RETURN_ERROR;
    }
    break;

    case RXIF_FREE:
    {
      if(args != NULL)
        FreeVecPooled(G->SharedMemPool, args);
    }
    break;
  }

  LEAVE();
}
