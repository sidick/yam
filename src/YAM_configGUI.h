#ifndef YAM_CONFIGGUI_H
#define YAM_CONFIGGUI_H

/***************************************************************************

 YAM - Yet Another Mailer
 Copyright (C) 1995-2000 Marcel Beck
 Copyright (C) 2000-2013 YAM Open Source Team

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

#include <exec/types.h>

// forward declarations
struct CO_ClassData;

Object *CO_PageFirstSteps(struct CO_ClassData *data);
Object *CO_PageTCPIP(struct CO_ClassData *data);
Object *CO_PageIdentities(struct CO_ClassData *data);
Object *CO_PageFilters(struct CO_ClassData *data);
Object *CO_PageSpam(struct CO_ClassData *data);
Object *CO_PageRead(struct CO_ClassData *data);
Object *CO_PageWrite(struct CO_ClassData *data);
Object *CO_PageReplyForward(struct CO_ClassData *data);
Object *CO_PageSignature(struct CO_ClassData *data);
Object *CO_PageSecurity(struct CO_ClassData *data);
Object *CO_PageStartupQuit(struct CO_ClassData *data);
Object *CO_PageMIME(struct CO_ClassData *data);
Object *CO_PageAddressBook(struct CO_ClassData *data);
Object *CO_PageScripts(struct CO_ClassData *data);
Object *CO_PageMixed(struct CO_ClassData *data);
Object *CO_PageLookFeel(struct CO_ClassData *data);
Object *CO_PageUpdate(struct CO_ClassData *data);

Object *MakeMimeTypePop(Object **string, const char *desc);

#endif /* YAM_CONFIGGUI_H */
