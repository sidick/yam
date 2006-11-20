/***************************************************************************

 YAM - Yet Another Mailer
 Copyright (C) 1995-2000 by Marcel Beck <mbeck@yam.ch>
 Copyright (C) 2000-2006 by YAM Open Source Team

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

 $Id:$

***************************************************************************/

#include <ctype.h>
#include <math.h>
#include <float.h>

#include <proto/dos.h>

#include "BayesFilter.h"
#include "HashTable.h"
#include "YAM_config.h"
#include "YAM_read.h"
#include "YAM_addressbook.h"
#include "YAM_utilities.h"
#include "extrasrc.h"

#include "Debug.h"

#define BAYES_TOKEN_DELIMITERS  " \t\n\r\f.,"
#define BAYES_MIN_TOKEN_LENGTH  3
#define BAYES_MAX_TOKEN_LENGTH  12

#define SPAMDATAFILE            ".spamdata"

// some compilers (vbcc) don't define this, so lets do it ourself
#ifndef M_LN2
#define M_LN2                   0.69314718055994530942
#endif

/*** Structure definitions ***/

struct Token
{
  struct HashEntryHeader hash;
  CONST_STRPTR word;
  ULONG length;
  ULONG count;
  double probability;
  double distance;
};

struct Tokenizer
{
  struct HashTable tokenTable;
};

struct TokenAnalyzer
{
  struct Tokenizer goodTokens;
  struct Tokenizer badTokens;
  ULONG goodCount;
  ULONG badCount;
  ULONG numDirtyingMessages;
};

struct TokenEnumeration
{
  ULONG entrySize;
  ULONG entryCount;
  ULONG entryOffset;
  STRPTR entryAddr;
  STRPTR entryLimit;
};

static struct TokenAnalyzer spamFilter;
static const TEXT magicCookie[] = { '\xFE', '\xED', '\xFA', '\xCE' };

/*** Static functions ***/
/// tokenizerInit()
//
static BOOL tokenizerInit(struct Tokenizer *t)
{
  BOOL result;

  ENTER();

  result = HashTableInit(&t->tokenTable, NULL, sizeof(struct Token), 1024);

  RETURN(result);
  return result;
}

///
/// tokenizerCleanup()
//
static void tokenizerCleanup(struct Tokenizer *t)
{
  ENTER();

  HashTableCleanup(&t->tokenTable);

  LEAVE();
}

///
/// tokenizerClearTokens()
//
static BOOL tokenizerClearTokens(struct Tokenizer *t)
{
  BOOL ok = TRUE;

  ENTER();

  if(t->tokenTable.entryStore != NULL)
  {
    tokenizerCleanup(t);
    ok = tokenizerInit(t);
  }

  RETURN(ok);
  return ok;
}

///
/// tokenizerGet()
//
static struct Token *tokenizerGet(struct Tokenizer *t, CONST_STRPTR word)
{
  struct HashEntryHeader *entry;

  ENTER();

  entry = HashTableOperate(&t->tokenTable, word, htoLookup);
  if(!HASH_ENTRY_IS_LIVE(entry))
    entry = NULL;

  RETURN(entry);
  return (struct Token *)entry;
}

///
/// tokenizerAdd()
//
static struct Token *tokenizerAdd(struct Tokenizer *t, CONST_STRPTR word, CONST_STRPTR prefix, ULONG count)
{
  struct Token *token = NULL;
  ULONG len;
  STRPTR tmpWord;

  ENTER();

  len = strlen(word) + 1;
  if(prefix != NULL)
    len += strlen(prefix) + 1;

  if((tmpWord = (STRPTR)malloc(len)) != NULL)
  {
    tmpWord[0] = '\0';
    if(prefix != NULL)
    {
      strlcat(tmpWord, prefix, len);
      strlcat(tmpWord, ":", len);
    }
    strlcat(tmpWord, word, len);

    if((token = (struct Token *)HashTableOperate(&t->tokenTable, tmpWord, htoAdd)) != NULL)
    {
      if(token->word == NULL)
      {
        if((token->word = strdup(tmpWord)) != NULL)
        {
          token->length = strlen(tmpWord);
          token->count = count;
          token->probability = 0.0;
        }
        else
          HashTableRawRemove(&t->tokenTable, (struct HashEntryHeader *)token);
      }
      else
       token->count += count;
    }

    free(tmpWord);
  }

  RETURN(token);
  return token;
}

///
/// tokenizerRemove()
static void tokenizerRemove(struct Tokenizer *t, CONST_STRPTR word, ULONG count)
{
  struct Token *token;

  ENTER();

  if((token = tokenizerGet(t, word)) != NULL)
  {
    if(token->count >= count)
    {
      token->count -= count;
      if(token->count == 0)
        HashTableRawRemove(&t->tokenTable, (struct HashEntryHeader *)token);
    }
  }

  LEAVE();
}

///
/// isDecimalNumber()
static BOOL isDecimalNumber(CONST_STRPTR word)
{
  CONST_STRPTR p = word;
  TEXT c;

  ENTER();

  if(*p == '-')
    p++;

  while((c = *p++) != '\0')
  {
    if(!isdigit(c))
    {
      RETURN(FALSE);
      return FALSE;
    }
  }

  RETURN(TRUE);
  return TRUE;
}

///
/// isASCII()
static BOOL isASCII(CONST_STRPTR word)
{
  CONST_STRPTR p = word;
  TEXT c;

  ENTER();

  while((c = *p++) != '\0')
  {
    if(c > 127)
    {
      RETURN(FALSE);
      return FALSE;
    }
  }

  RETURN(TRUE);
  return TRUE;
}

///
/// tokenizerAddTokenForHeader()
//
static void tokenizerAddTokenForHeader(struct Tokenizer *t, CONST_STRPTR prefix, STRPTR value, BOOL tokenizeValue)
{
  ENTER();

  if(value != NULL && strlen(value) > 0)
  {
    ToLowerCase(value);
    if(!tokenizeValue)
      tokenizerAdd(t, value, prefix, 1);
    else
    {
      STRPTR word = value, next;

      do
      {
        if((next = strpbrk(word, BAYES_TOKEN_DELIMITERS)) != NULL)
          *next++ = '\0';

        if(word[0] != '\0' && !isDecimalNumber(word))
        {
          if(isASCII(word))
            tokenizerAdd(t, word, prefix, 1);
        }

        word = next;
      }
      while (word != NULL);
    }
  }

  LEAVE();
}

///
/// tokenizerTokenizeAttachment
//
static void tokenizerTokenizeAttachment(struct Tokenizer *t, STRPTR contentType, STRPTR fileName)
{
  STRPTR tmpContentType;
  STRPTR tmpFileName;

  ENTER();

  tmpContentType = strdup(contentType);
  tmpFileName = strdup(fileName);

  if(tmpContentType != NULL && tmpFileName != NULL)
  {
    ToLowerCase(tmpContentType);
    ToLowerCase(tmpFileName);
    tokenizerAddTokenForHeader(t, "attachment/filename", tmpFileName, FALSE);
    tokenizerAddTokenForHeader(t, "attachment/content-type", tmpContentType, FALSE);
  }

  if(tmpContentType)
    free(tmpContentType);

  if(tmpFileName)
    free(tmpFileName);

  LEAVE();
}

///
/// tokenizerTokenizeHeader()
//
static void tokenizerTokenizeHeaders(struct Tokenizer *t, struct Part *part)
{
  struct MinNode *node;
  STRPTR contentType, charSet;

  ENTER();

  contentType = (part->ContentType != NULL) ? strdup(part->ContentType) : NULL;
  charSet = (part->CParCSet != NULL) ? strdup(part->CParCSet) : NULL;

  if(contentType != NULL)
    ToLowerCase(contentType);

  if(charSet != NULL)
    ToLowerCase(charSet);

  for(node = part->headerList->mlh_Head; node->mln_Succ != NULL; node = node->mln_Succ)
  {
    struct HeaderNode *hdr = (struct HeaderNode *)node;
    STRPTR name;
    STRPTR content;

    name = strdup(hdr->name);
    content = strdup(hdr->content);

    if(name != NULL && content != NULL)
    {
      ToLowerCase(name);
      ToLowerCase(content);

      SHOWSTRING(DBF_SPAM, name);
      SHOWSTRING(DBF_SPAM, content);

      switch(name[0])
      {
        case 'c':
        {
          if(strcmp(name, "content-type") == 0)
          {
            tokenizerAddTokenForHeader(t, "content-type", contentType, FALSE);
            tokenizerAddTokenForHeader(t, "charset", charSet, FALSE);
          }
          break;

          case 'r':
          {
            if(strcmp(name, "received") == 0 &&
               strstr(content, "may be forged") != NULL)
            {
              tokenizerAddTokenForHeader(t, name, (STRPTR)"may be forged", FALSE);
            }

            // leave out reply-to
          }
          break;

          case 's':
          {
            // we want to tokenize the subject
            if(strcmp(name, "subject") == 0)
              tokenizerAddTokenForHeader(t, name, content, TRUE);

            // leave out sender field, too strong of an indicator
          }
          break;

          case 'u':
          case 'x':
          {
            // X-Mailer/User-Agent works best if it is untokenized
            // just fold the case and any leading/trailing white space
            tokenizerAddTokenForHeader(t, name, content, FALSE);
          }
          break;

          default:
          {
            tokenizerAddTokenForHeader(t, name, content, FALSE);
          }
          break;
        }
      }

      free(name);
      free(content);
    }
  }

  if(contentType != NULL)
    free(contentType);

  if(charSet != NULL)
    free(charSet);

  LEAVE();
}

///
/// countChars()
//
static ULONG countChars(STRPTR str, TEXT c)
{
  ULONG count = 0;
  TEXT cc;

  ENTER();

  while ((cc = *str++) != '\0')
  {
    if(cc == c)
      count++;
  }

  RETURN(count);
  return count;
}

///
/// tokenizerTokenizeASCIIWord()
//
static void tokenizerTokenizeASCIIWord(struct Tokenizer *t, STRPTR word)
{
  ULONG length;

  ENTER();

  ToLowerCase(word);
  length = strlen(word);

  // if the word fits in our length restrictions then we add it
  if(length >= BAYES_MIN_TOKEN_LENGTH && length <= BAYES_MAX_TOKEN_LENGTH)
    tokenizerAdd(t, word, NULL, 1);
  else
  {
    BOOL skipped = TRUE;

    // don't skip over the word if it looks like an email address
    if(length > BAYES_MAX_TOKEN_LENGTH)
    {
      if(length < 40 && strchr((CONST_STRPTR)word, '.') != NULL && countChars(word, '@') == 1)
      {
        struct Person pe;

        // split the john@foo.com into john and foo.com, treat them as separate tokens
        ExtractAddress(word, &pe);

        if(pe.Address[0] != '\0' && pe.RealName[0] != '\0')
        {
          SHOWSTRING(DBF_SPAM, pe.Address);
          SHOWSTRING(DBF_SPAM, pe.RealName);

          tokenizerAdd(t, pe.Address, "email-addr", 1);
          tokenizerAdd(t, pe.RealName, "email-name", 1);
          skipped = FALSE;
        }
      }
    }

    // there is value in generating a token indicating the number of characters we are skipping
    // we'll round to the nearest of 10
    if(skipped)
    {
      TEXT buffer[40];

      snprintf(buffer, sizeof(buffer), "%c %ld", word[0], (length / 10) * 10);
      tokenizerAdd(t, buffer, "skip", 1);
    }
  }

  LEAVE();
}

///
/// tokenizeTokenize()
//
static void tokenizerTokenize(struct Tokenizer *t, STRPTR text)
{
  STRPTR word = text;
  STRPTR next;

  ENTER();

  do
  {
    if((next = strpbrk(word, BAYES_TOKEN_DELIMITERS)) != NULL)
      *next++ = '\0';

    if(word[0] != '\0' && !isDecimalNumber(word))
    {
      if(isASCII(word))
        tokenizerTokenizeASCIIWord(t, word);
      else
        tokenizerAdd(t, word, NULL, 1);
    }

    word = next;
  }
  while (word != NULL);

  LEAVE();
}

///
/// tokenEnumerationInit()
//
static void tokenEnumerationInit(struct TokenEnumeration *te, struct Tokenizer *t)
{
  ENTER();

  te->entrySize = t->tokenTable.entrySize;
  te->entryCount = t->tokenTable.entryCount;
  te->entryOffset = 0;
  te->entryAddr = t->tokenTable.entryStore;
  te->entryLimit = te->entryAddr + HASH_TABLE_SIZE(&t->tokenTable) * te->entrySize;

  LEAVE();
}

///
/// tokenEnumerationNext()
//
static struct Token *tokenEnumerationNext(struct TokenEnumeration *te)
{
  struct Token *token = NULL;

  ENTER();

  if(te->entryOffset < te->entryCount)
  {
    ULONG entrySize = te->entrySize;
    STRPTR entryAddr = te->entryAddr;
    STRPTR entryLimit = te->entryLimit;

    while(entryAddr < entryLimit)
    {
      struct HashEntryHeader *entry = (struct HashEntryHeader *)entryAddr;

      entryAddr += entrySize;

      if(HASH_ENTRY_IS_LIVE(entry))
      {
        token = (struct Token *)entry;
        te->entryOffset++;
        break;
      }
    }

    te->entryAddr = entryAddr;
  }

  RETURN(token);
  return token;
}

///
/// tokenizerCopyTokens()
//
static struct Token *tokenizerCopyTokens(struct Tokenizer *t)
{
  struct Token *tokens = NULL;
  ULONG count = t->tokenTable.entryCount;

  ENTER();

  if(count > 0)
  {
    if((tokens = (struct Token *)malloc(count * sizeof(*tokens))) != NULL)
    {
      struct TokenEnumeration te;
      struct Token *tp = tokens, *token;

      tokenEnumerationInit(&te, t);
      while((token = tokenEnumerationNext(&te)) != NULL)
        *tp++ = *token;
    }
  }

  RETURN(tokens);
  return tokens;
}

///
/// tokenizerForgetTokens()
//
static void tokenizerForgetTokens(struct Tokenizer *t, struct TokenEnumeration *te)
{
  struct Token *token;

  ENTER();

  // if we are forgetting the tokens for a message, should only substract 1 from the occurence
  // count for that token in the training set, because we assume we only bumped the training
  // set count once per message containing the token
  while((token = tokenEnumerationNext(te)) != NULL)
    tokenizerRemove(t, token->word, 1);

  LEAVE();
}

///
/// tokenizerRememberTokens()
//
static void tokenizerRememberTokens(struct Tokenizer *t, struct TokenEnumeration *te)
{
  struct Token *token;

  ENTER();

  while((token = tokenEnumerationNext(te)) != NULL)
    tokenizerAdd(t, token->word, NULL, 1);

  LEAVE();
}

///
/// tokenAnalyzerInit()
//
static BOOL tokenAnalyzerInit(struct TokenAnalyzer *ta)
{
  BOOL result = FALSE;

  ENTER();

  ta->goodCount = 0;
  ta->badCount = 0;
  ta->numDirtyingMessages = 0;

  if(tokenizerInit(&ta->goodTokens) && tokenizerInit(&ta->badTokens))
    result = TRUE;

  RETURN(result);
  return result;
}

///
/// tokenAnalyzerCleanup()
//
static void tokenAnalyzerCleanup(struct TokenAnalyzer *ta)
{
  ENTER();

  tokenizerCleanup(&ta->goodTokens);
  tokenizerCleanup(&ta->badTokens);

  LEAVE();
}

///
/// writeUInt32()
//
static ULONG writeUInt32(FILE *stream, ULONG value)
{
  ULONG n;

  ENTER();

  value = htonl(value);
    
  n = fwrite(&value, sizeof(value), 1, stream);

  RETURN(n);
  return n;
}

///
/// readUInt32()
//
static ULONG readUInt32(FILE *stream, ULONG *value)
{
  ULONG n;

  ENTER();

  if((n = fread(value, sizeof(*value), 1, stream)) == 1)
    *value = ntohl(*value);

  RETURN(n);
  return n;
}

///
/// writeTokens()
//
static BOOL writeTokens(FILE *stream, struct Tokenizer *t)
{
  ULONG tokenCount = t->tokenTable.entryCount;

  ENTER();

  if(writeUInt32(stream, tokenCount) != 1)
  {
    RETURN(FALSE);
    return FALSE;
  }

  if(tokenCount > 0)
  {
    struct TokenEnumeration te;
    ULONG i;

    tokenEnumerationInit(&te, t);
    for(i = 0; i < tokenCount; i++)
    {
      struct Token *token = tokenEnumerationNext(&te);
      ULONG length = token->length;

      if(writeUInt32(stream, token->count) != 1)
        break;

      if(writeUInt32(stream, length) != 1)
        break;

      if(fwrite(token->word, length, 1, stream) != 1)
        break;
    }
  }

  RETURN(TRUE);
  return TRUE;
}

///
/// readTokens()
//
static BOOL readTokens(FILE *stream, struct Tokenizer *t)
{
  ULONG tokenCount;
  ULONG bufferSize = 4096;
  STRPTR buffer;

  ENTER();

  if(readUInt32(stream, &tokenCount) != 1)
  {
    RETURN(FALSE);
    return FALSE;
  }

  if((buffer = malloc(bufferSize)) != NULL)
  {
    ULONG i;

    for(i = 0; i < tokenCount; i++)
    {
      ULONG count;
      ULONG size;

      if(readUInt32(stream, &count) != 1)
        break;

      if(readUInt32(stream, &size) != 1)
        break;

      if(size >= bufferSize)
      {
        ULONG newBufferSize = 2 * bufferSize;

        free(buffer);

        while(size >= newBufferSize)
          newBufferSize *= 2;

        if((buffer = malloc(newBufferSize)) == NULL)
        {
          RETURN(FALSE);
          return FALSE;
        }

        bufferSize = newBufferSize;
      }

      if(fread(buffer, size, 1, stream) != 1)
        break;

      buffer[size] = '\0';

      tokenizerAdd(t, buffer, NULL, count);
    }

    free(buffer);
  }

  RETURN(TRUE);
  return TRUE;
}

///
/// tokenAnalyzerWriteTraningData()
//
static void tokenAnalyzerWriteTrainingData(struct TokenAnalyzer *ta)
{
  char fname[SIZE_PATHFILE];
  FILE *stream;

  ENTER();

  // prepare the filename for saving
  strmfp(fname, G->MA_MailDir, SPAMDATAFILE);

  // open the .spamdata file for binary write
  if((stream = fopen(fname, "wb")) != NULL)
  {
    if(fwrite(magicCookie, sizeof(magicCookie), 1, stream) == 1 &&
       writeUInt32(stream, ta->goodCount) == 1 &&
       writeUInt32(stream, ta->badCount) == 1 &&
       writeTokens(stream, &ta->goodTokens) == 1 &&
       writeTokens(stream, &ta->badTokens) == 1)
    {
      fclose(stream);
      ta->numDirtyingMessages = 0;
    }
    else
    {
      // anything went wrong, so delete the training data file
      fclose(stream);

      DeleteFile(fname);
    }
  }

  LEAVE();
}

///
/// tokenAnalyzerReadTrainingData()
//
static void tokenAnalyzerReadTrainingData(struct TokenAnalyzer *ta)
{
  char fname[SIZE_PATHFILE];
  FILE *stream;

  ENTER();

  // prepare the filename for loading
  strmfp(fname, G->MA_MailDir, SPAMDATAFILE);

  // open the .spamdata file for binary read
  if((stream = fopen(fname, "rb")) != NULL)
  {
    TEXT cookie[4];

    fread(cookie, sizeof(cookie), 1, stream);

    if(memcmp(cookie, magicCookie, sizeof(cookie)) == 0)
    {
      readUInt32(stream, &ta->goodCount);
      SHOWVALUE(DBF_SPAM, ta->goodCount);

      readUInt32(stream, &ta->badCount);
      SHOWVALUE(DBF_SPAM, ta->badCount);

      readTokens(stream, &ta->goodTokens);
      readTokens(stream, &ta->badTokens);
    }

    fclose(stream);
  }

  LEAVE();
}

///
/// tokenAnalyzerResetTrainingData()
//
static void tokenAnalyzerResetTrainingData(struct TokenAnalyzer *ta)
{
  char fname[SIZE_PATHFILE];

  ENTER();

  if(ta->goodCount != 0 || ta->goodTokens.tokenTable.entryCount != 0)
  {
    tokenizerClearTokens(&ta->goodTokens);
    ta->goodCount = 0;
  }

  if(ta->badCount != 0 || ta->badTokens.tokenTable.entryCount != 0)
  {
    tokenizerClearTokens(&ta->badTokens);
    ta->badCount = 0;
  }

  // prepare the filename for analysis
  strmfp(fname, G->MA_MailDir, SPAMDATAFILE);

  if(FileExists(fname))
    DeleteFile(fname);

  LEAVE();
}

///
/// tokenAnalyzerSetClassification()
//
static void tokenAnalyzerSetClassification(struct TokenAnalyzer *ta,
                                           struct Tokenizer *t,
                                           enum BayesClassification oldClass,
                                           enum BayesClassification newClass)
{
  struct TokenEnumeration te;

  ENTER();

  tokenEnumerationInit(&te, t);

  if(oldClass != newClass)
  {
    switch(oldClass)
    {
      case BC_SPAM:
      {
        // remove tokens from spam corpus
        if(ta->badCount > 0)
        {
          ta->badCount--;
          ta->numDirtyingMessages++;
          tokenizerForgetTokens(&ta->badTokens, &te);
        }
      }
      break;

      case BC_HAM:
      {
        // remove tokens from ham corpus
        if(ta->goodCount > 0)
        {
          ta->goodCount--;
          ta->numDirtyingMessages++;
          tokenizerForgetTokens(&ta->goodTokens, &te);
        }
      }
      break;

      case BC_OTHER:
        // nothing
      break;
    }

    switch(newClass)
    {
      case BC_SPAM:
      {
        // put tokens into spam corpus
        ta->badCount++;
        ta->numDirtyingMessages++;
        tokenizerRememberTokens(&ta->badTokens, &te);
      }
      break;

      case BC_HAM:
      {
        // put tokens into ham corpus
        ta->goodCount++;
        ta->numDirtyingMessages++;
        tokenizerRememberTokens(&ta->goodTokens, &te);
      }
      break;

      case BC_OTHER:
        // nothing
      break;
    }
  }

  LEAVE();
}

///
/// compareTokens()
//
static int compareTokens(CONST void *p1, CONST void *p2)
{
  struct Token *t1 = (struct Token *)p1;
  struct Token *t2 = (struct Token *)p2;
  double delta = t1->distance - t2->distance;
  int cmp;

  ENTER();

  cmp = ((delta == 0.0) ? 0 : ((delta > 0.0) ? 1 : -1));

  RETURN(cmp);
  return cmp;
}

///

static const double C_1 = 1.0 / 12.0;
static const double C_2 = -1.0 / 360.0;
static const double C_3 = 1.0 / 1260.0;
static const double C_4 = -1.0 / 1680.0;
static const double C_5 = 1.0 / 1188.0;
static const double C_6 = -691.0 / 360360.0;
static const double C_7 = 1.0 / 156.0;
static const double C_8 = -3617.0 / 122400.0;
static const double C_9 = 43867.0 / 244188.0;
static const double C_10 = -174611.0 / 125400.0;
static const double C_11 = 77683.0 / 5796.0;

/// lngamma_asymp()
// truncated asymptotic series in 1/z
INLINE double lngamma_asymp(double z)
{
  double w, w2, sum;

  w = 1.0 / z;
  w2 = w * w;
  sum = w * (w2 * (w2 * (w2 * (w2 * (w2 * (w2 * (w2 * (w2 * (w2
        * (C_11 * w2 + C_10) + C_9) + C_8) + C_7) + C_6)
        + C_5) + C_4) + C_3) + C_2) + C_1);

  return sum;
}
///

struct fact_table_s
{
  double fact;
  double lnfact;
};

// for speed and accuracy
static const struct fact_table_s FactTable[] =
{
  { 1.000000000000000, 0.0000000000000000000000e+00 },
  { 1.000000000000000, 0.0000000000000000000000e+00 },
  { 2.000000000000000, 6.9314718055994530942869e-01 },
  { 6.000000000000000, 1.7917594692280550007892e+00 },
  { 24.00000000000000, 3.1780538303479456197550e+00 },
  { 120.0000000000000, 4.7874917427820459941458e+00 },
  { 720.0000000000000, 6.5792512120101009952602e+00 },
  { 5040.000000000000, 8.5251613610654142999881e+00 },
  { 40320.00000000000, 1.0604602902745250228925e+01 },
  { 362880.0000000000, 1.2801827480081469610995e+01 },
  { 3628800.000000000, 1.5104412573075515295248e+01 },
  { 39916800.00000000, 1.7502307845873885839769e+01 },
  { 479001600.0000000, 1.9987214495661886149228e+01 },
  { 6227020800.000000, 2.2552163853123422886104e+01 },
  { 87178291200.00000, 2.5191221182738681499610e+01 },
  { 1307674368000.000, 2.7899271383840891566988e+01 },
  { 20922789888000.00, 3.0671860106080672803835e+01 },
  { 355687428096000.0, 3.3505073450136888885825e+01 },
  { 6402373705728000., 3.6395445208033053576674e+01 }
};

#define FactTableLength (int)(sizeof(FactTable)/sizeof(FactTable[0]))

// for speed
static const double ln_2pi_2 = 0.918938533204672741803; // log(2*PI)/2

/// nsLnGamma()
// A simple lgamma function, not very robust.
//
// Valid for z_in > 0 ONLY.
//
// For z_in > 8 precision is quite good, relative errors < 1e-14 and
// usually better. For z_in < 8 relative errors increase but are usually
// < 1e-10. In two small regions, 1 +/- .001 and 2 +/- .001 errors
// increase quickly.
static double nsLnGamma (double z_in, int *gsign)
{
  double scale, z, sum, result;
  int zi = (int) z_in;
  *gsign = 1;

  if(z_in == (double) zi)
  {
    if(0 < zi && zi <= FactTableLength)
      return FactTable[zi - 1].lnfact;    // gamma(z) = (z-1)!
  }

  for(scale = 1.0, z = z_in; z < 8.0; ++z)
    scale *= z;

  sum = lngamma_asymp (z);
  result = (z - 0.5) * log (z) - z + ln_2pi_2 - log (scale);
  result += sum;

  return result;
}

///
/// lnPQfactoer()
// log( e^(-x)*x^a/Gamma(a) )
INLINE double lnPQfactor (double a, double x)
{
  int gsign;                // ignored because a > 0
  return a * log (x) - x - nsLnGamma (a, &gsign);
}

///
/// Pseries()
static double Pseries (double a, double x, int *error)
{
  double sum, term;
  const double eps = 2.0 * DBL_EPSILON;
  const int imax = 5000;
  int i;

  sum = term = 1.0 / a;
  for(i = 1; i < imax; ++i)
  {
      term *= x / (a + i);
      sum += term;
      if (fabs (term) < eps * fabs (sum))
      break;
  }

  if (i >= imax)
      *error = 1;

  return sum;
}

///
/// Qcontfrac()
//
static double Qcontfrac (double a, double x, int *error)
{
  double result, D, C, e, f, term;
  const double eps = 2.0 * DBL_EPSILON;
  const double small = DBL_EPSILON * DBL_EPSILON * DBL_EPSILON * DBL_EPSILON;
  const int imax = 5000;
  int i;

  // modified Lentz method
  f = x - a + 1.0;
  if(fabs (f) < small)
    f = small;
  C = f + 1.0 / small;
  D = 1.0 / f;
  result = D;
  for(i = 1; i < imax; ++i)
  {
    e = i * (a - i);
    f += 2.0;
    D = f + e * D;
    if(fabs (D) < small)
      D = small;
    D = 1.0 / D;
    C = f + e / C;
    if(fabs (C) < small)
      C = small;
    term = C * D;
    result *= term;
    if(fabs (term - 1.0) < eps)
      break;
  }

  if(i >= imax)
    *error = 1;

  return result;
}

///
/// incompleteGammaP()
//
double incompleteGammaP( double a, double x, int *error )
{
  double result, dom, ldom;

  //  domain errors. the return values are meaningless but have
  //  to return something.
  *error = -1;
  if(a <= 0.0)
    return 1.0;

  if(x < 0.0)
    return 0.0;

  *error = 0;
  if(x == 0.0)
    return 0.0;

  ldom = lnPQfactor (a, x);
  dom = exp(ldom);

  // might need to adjust the crossover point
  if(a <= 0.5)
  {
    if(x < a + 1.0)
      result = dom * Pseries (a, x, error);
    else
      result = 1.0 - dom * Qcontfrac (a, x, error);
  }
  else
  {
    if(x < a)
      result = dom * Pseries (a, x, error);
    else
      result = 1.0 - dom * Qcontfrac (a, x, error);
  }

  // not clear if this can ever happen
  if(result > 1.0)
    result = 1.0;

  if(result < 0.0)
    result = 0.0;

  return result;
}

///
/// chi2P()
//
INLINE double chi2P(double chi2, double nu, int *error)
{
  if(chi2 < 0.0 || nu < 0.0)
  {
    *error = -1;
    return 0.0;
  }

  return incompleteGammaP(nu / 2.0, chi2 / 2.0, error);
}

///
/// tokenAnalyzerClassifyMessage()
//
static BOOL tokenAnalyzerClassifyMessage(struct TokenAnalyzer *ta, struct Tokenizer *t, struct Mail *mail)
{
  BOOL isSpam = FALSE;

  ENTER();

  // if the sender address is in the address book then we assume it's not spam
  if(C->SpamAddressBookIsWhiteList == FALSE || AB_FindEntry(mail->From.Address, ABF_RX_EMAIL, NULL) == 0)
  {
    struct Token *tokens = NULL;

    SHOWVALUE(DBF_SPAM, ta->goodCount);
    SHOWVALUE(DBF_SPAM, ta->badCount);

    if((tokens = tokenizerCopyTokens(t)) != NULL)
    {
      double nGood = ta->goodCount;

      if(nGood != 0 || ta->goodTokens.tokenTable.entryCount != 0)
      {
        double nBad = ta->badCount;

        if(nBad != 0 || ta->badTokens.tokenTable.entryCount != 0)
        {
          ULONG i;
          ULONG goodClues = 0;
          ULONG count = t->tokenTable.entryCount;
          ULONG first;
          ULONG last;
          ULONG Hexp;
          ULONG Sexp;
          double prob;
          double H;
          double S;
          int e;

          for(i=0; i < count; i++)
          {
            struct Token *token = &tokens[i];
            CONST_STRPTR word = (CONST_STRPTR)token->word;
            struct Token *t;
            double hamCount;
            double spamCount;
            double denom;
            double prob;
            double n;
            double distance;

            t = tokenizerGet(&ta->goodTokens, word);
            hamCount = (t != NULL) ? t->count : 0;
            t = tokenizerGet(&ta->badTokens, word);
            spamCount = (t != NULL) ? t->count : 0;

            denom = hamCount * nBad + spamCount * nGood;
            if(denom == 0.0)
              denom = nBad + nGood;

            prob = (spamCount * nGood) / denom;
            n = hamCount + spamCount;
            prob = (0.225 + n * prob) / (0.45 + n);
            distance = fabs(prob - 0.5);

            if(distance >= 0.1)
            {
              goodClues++;
              token->distance = distance;
              token->probability = prob;
            }
            else
            {
              // ignore clue
              token->distance = -1;
            }
          }

          // sort array of token distances
          qsort(tokens, count, sizeof(*tokens), compareTokens);

          first = (goodClues > 150) ? count - 150 : 0;
          last = count;
          H = 1.0;
          S = 1.0;
          Hexp = 0;
          Sexp = 0;
          e = 0;

          for(i = first; i < last; ++i)
          {
            if(tokens[i].distance != -1)
            {
              double value;

              goodClues++;
              value = tokens[i].probability;
              S *= (1.0 - value);
              H *= value;

              if(S < 1e-200)
              {
                S = frexp(S, &e);
                Sexp += e;
              }

              if(H < 1e-200)
              {
                H = frexp(H, &e);
                Hexp += e;
              }
            }
          }

          S = log(S) + Sexp * M_LN2;
          H = log(H) + Hexp * M_LN2;

          SHOWVALUE(DBF_SPAM, goodClues);
          if(goodClues > 0)
          {
            int chiError;

            S = chi2P(-2.0 * S, 2.0 * goodClues, &chiError);

            if(!chiError)
              H = chi2P(-2.0 * H, 2.0 * goodClues, &chiError);

            // if any error, then toss the complete calculation
            if(chiError)
            {
              D(DBF_SPAM, "chi2P error");
              prob = 0.5;
            }
            else
              prob = (S - H + 1.0) / 2.0;
          }
          else
            prob = 0.5;

          D(DBF_SPAM, "mail with subject \"%s\" has spam probability %.2f, ham score: %.2f, spam score: %.2f", mail->Subject, prob, H, S);
          isSpam = (prob * 100 >= C->SpamProbabilityThreshold);
        }
      }
      else
        isSpam = TRUE; // assume SPAM

      free(tokens);
    }
  }

  RETURN(isSpam);
  return isSpam;
}

///

/// BayesFilterInit()
//
BOOL BayesFilterInit(void)
{
  BOOL result = FALSE;

  ENTER();

  if(tokenAnalyzerInit(&spamFilter))
  {
    if(C->SpamFilterEnabled)
      tokenAnalyzerReadTrainingData(&spamFilter);

    result = TRUE;
  }

  RETURN(result);
  return result;
}

///
/// BayesFilterCleanup()
//
void BayesFilterCleanup(void)
{
  ENTER();

  if(C->SpamFilterEnabled)
  {
    // write the spam training data to disk
    tokenAnalyzerWriteTrainingData(&spamFilter);
  }
  else
  {
    char fname[SIZE_PATHFILE];

    // prepare the filename for saving
    strmfp(fname, G->MA_MailDir, SPAMDATAFILE);

    // the spam filter is not enabled, so delete the training data, if they exist
    if(FileExists(fname))
      DeleteFile(fname);
  }

  tokenAnalyzerCleanup(&spamFilter);

  LEAVE();
}

///
/// tokenizeMail()
//
static void tokenizeMail(struct Tokenizer *t, struct Mail *mail)
{
  struct ReadMailData *rmData;

  ENTER();

  if((rmData = AllocPrivateRMData(mail, PM_ALL)))
  {
    STRPTR rptr;

    if((rptr = RE_ReadInMessage(rmData, RIM_QUIET)) != NULL)
    {
      // first tokenize all texts
      tokenizerTokenize(t, rptr);

      if(isMultiPartMail(mail))
      {
        struct Part *part;

        // iterate through all mail parts to tokenize attachments, too
        for(part = rmData->firstPart; part != NULL; part = part->Next)
        {
          if(part->headerList != NULL)
            tokenizerTokenizeHeaders(t, part);

          if(part->Nr > PART_RAW && part->Nr != rmData->letterPartNum && (part->isAltPart == FALSE || part->Parent == NULL || part->Parent->MainAltPart == part))
            tokenizerTokenizeAttachment(t, part->ContentType, part->Filename);
        }
      }

      free(rptr);
    }

    FreePrivateRMData(rmData);
  }

  LEAVE();
}

///
/// BayesFilterClassifyMessage()
//
BOOL BayesFilterClassifyMessage(struct Mail *mail)
{
  BOOL isSpam = FALSE;
  struct Tokenizer t;

  ENTER();

  if(tokenizerInit(&t))
  {
    tokenizeMail(&t, mail);

    isSpam = tokenAnalyzerClassifyMessage(&spamFilter, &t, mail);
        
    tokenizerCleanup(&t);
  }

  RETURN(isSpam);
  return isSpam;
}

///
/// BayesFilterSetClassification()
//
void BayesFilterSetClassification(struct Mail *mail, enum BayesClassification newClass)
{
  struct Tokenizer t;

  ENTER();

  if(tokenizerInit(&t))
  {
    enum BayesClassification oldClass;

    if(hasStatusUserSpam(mail))
      oldClass = BC_SPAM;
    else if(hasStatusHam(mail))
      oldClass = BC_HAM;
    else
      oldClass = BC_OTHER;

    tokenizeMail(&t, mail);

    // now we invert the current classification
    tokenAnalyzerSetClassification(&spamFilter, &t, oldClass, newClass);

    tokenizerCleanup(&t);
  }

  LEAVE();
}

///
/// BayesFilterNumerOfSpamClassifiedMails()
//
ULONG BayesFilterNumberOfSpamClassifiedMails(void)
{
  ULONG num;

  ENTER();

  num = spamFilter.badCount;

  RETURN(num);
  return num;
}

///
/// BayesFilterNumberOfHamClassifiedMails()
//
ULONG BayesFilterNumberOfHamClassifiedMails(void)
{
  ULONG num;

  ENTER();

  num = spamFilter.goodCount;

  RETURN(num);
  return num;
}

///
/// BayesFilterFlushTrainingData()
//
void BayesFilterFlushTrainingData(void)
{
  ENTER();

  if(C->SpamFlushTrainingDataThreshold > 0 && spamFilter.numDirtyingMessages > (ULONG)C->SpamFlushTrainingDataThreshold)
  {
    tokenAnalyzerWriteTrainingData(&spamFilter);
    spamFilter.numDirtyingMessages = 0;
  }

  LEAVE();
}

///
/// BayesFilterResetTrainingData()
//
void BayesFilterResetTrainingData(void)
{
  ENTER();

  tokenAnalyzerResetTrainingData(&spamFilter);

  LEAVE();
}

///
