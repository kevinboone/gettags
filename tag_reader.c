/*==========================================================================
gettags
tag_reader.c
Copyright (c)2012-2018 Kevin Boone
Distributed under the terms of the GNU Public Licence, v3.0
==========================================================================*/
  
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdlib.h>
#include "types.h"
#include "tag_reader.h"

// Historical file open flag from the Windows days
#define O_BINARY 0

// Set this to true for lots of incomprehensible debug gibberish 
BOOL tag_debug = FALSE; 

/**********************************************************************
  UNICODE SUPPORT
*********************************************************************/

/*
 * The following block of code is for converting Unicode formats, and was
 * mercilessly ripped out of the Unicode Consortium's sample code. Please
 * don't ask me how it works, because I don't know. In particular, I am
 * not sure whether we need to take further steps to deal with byte order
 * conversion
 */
typedef unsigned int UTF32;
typedef unsigned short UTF16;
typedef unsigned char UTF8;
static const int halfShift  = 10; 
static const UTF32 halfBase = 0x0010000UL;
#define UNI_REPLACEMENT_CHAR (UTF32)0x0000FFFD
#define UNI_SUR_HIGH_START  (UTF32)0xD800
#define UNI_SUR_HIGH_END    (UTF32)0xDBFF
#define UNI_SUR_LOW_START   (UTF32)0xDC00
#define UNI_SUR_LOW_END     (UTF32)0xDFFF
static const UTF8 firstByteMark[7] = 
  { 0x00, 0x00, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC };

typedef enum 
  {
  conversionOK,   /* conversion successful */
  sourceExhausted, /* partial character in source, but hit end */
  targetExhausted, /* insuff. room in target for conversion */
  sourceIllegal  /* source sequence is illegal/malformed */
  } UTFConversionResult;

static UTFConversionResult _convert_utf16_to_utf8 (const UTF16** sourceStart, 
      const UTF16* sourceEnd, UTF8** targetStart, UTF8* targetEnd) 
{
  UTFConversionResult result = conversionOK;
  const UTF16* source = *sourceStart;
  UTF8* target = *targetStart;
  while (source < sourceEnd) 
  {
    UTF32 ch;
    unsigned short bytesToWrite = 0;
    const UTF32 byteMask = 0xBF;
    const UTF32 byteMark = 0x80; 
    const UTF16* oldSource = source; 
    ch = *source++;
    if (ch >= UNI_SUR_HIGH_START && ch <= UNI_SUR_HIGH_END) 
     {
       if (source < sourceEnd) 
       {
         UTF32 ch2 = *source;
         if (ch2 >= UNI_SUR_LOW_START && ch2 <= UNI_SUR_LOW_END) 
         {
           ch = ((ch - UNI_SUR_HIGH_START) << halfShift)
              + (ch2 - UNI_SUR_LOW_START) + halfBase;
           ++source;
         } 
       } 
       else 
       { 
         --source; 
         result = sourceExhausted;
         break;
       }
     } 
     if (ch < (UTF32)0x80) 
     {
         bytesToWrite = 1;
      } 
      else if (ch < (UTF32)0x800) 
      {     
        bytesToWrite = 2;
      } 
      else if (ch < (UTF32)0x10000) 
      {   
        bytesToWrite = 3;
      } 
      else if (ch < (UTF32)0x110000) 
      {  
        bytesToWrite = 4;
      } 
      else 
      {
         bytesToWrite = 3;
         ch = UNI_REPLACEMENT_CHAR;
      }

      target += bytesToWrite;
      if (target > targetEnd) 
      {
        source = oldSource; 
        target -= bytesToWrite; result = targetExhausted; 
        break;
      }
      switch (bytesToWrite) 
      { /* note: everything falls through. */
        case 4: *--target = (UTF8)((ch | byteMark) & byteMask); ch >>= 6;
        case 3: *--target = (UTF8)((ch | byteMark) & byteMask); ch >>= 6;
        case 2: *--target = (UTF8)((ch | byteMark) & byteMask); ch >>= 6;
        case 1: *--target =  (UTF8)(ch | firstByteMark[bytesToWrite]);
      }
      target += bytesToWrite;
    }
  *sourceStart = source;
  *targetStart = target;
  return result;
}

// Caller must free string returned
// len is the length in bytes of the UTF16, including the BOM if present
static char *tag_convert_utf16_to_utf8 (int has_bom, const UTF16 *s, int len)
{
  // Guess final UTF8 size -- not going to be 
  // more than double the UTF16

  int utf8_len = len * 2;
  UTF8 *target = (UTF8 *) malloc (utf8_len);   
  memset (target, 0, utf8_len);
  UTF8 *t = target;

  // Do we need to swap byte order as well as skipping the BOM? 
  const UTF16 *start = s;

  int utf16_len; 
  if (has_bom) 
    {
    utf16_len = len / sizeof (UTF16) - 1;
    start += 1;
    //utf16_len -= 1;
    }
  else
    {
    utf16_len = len / sizeof (UTF16);
    }

  UTFConversionResult ret = _convert_utf16_to_utf8
    (&start, start + utf16_len, 
     &t, t + utf8_len);
  if (ret != conversionOK )
    strncpy ((char *)target, "Unicode error", utf8_len - 1);

  return (char*) target;
}

static unsigned char *tag_convert_iso8859_to_utf8 
  (const unsigned char *s, int len)
{
  unsigned char *buff = (unsigned char *) malloc (len * 2); // Worst case
  memset (buff, 0, len * 2);
  unsigned char *out = buff;
  
  int i = 0;
  while (i < len)
  {
    if (s[i] < 128) { *out++ = s[i]; }
    else { *out++ = (0xc2+(s[i]>0xbf)); *out++ = ((s[i]&0x3f)+0x80); }
  i++;
  }
  return buff;
}

/**********************************************************************
  MP3/ID3v2 SUPPORT
*********************************************************************/

/*
 * Read the next frame. f is a file handle open at the start of the
 * frame. Version is the ID3v2 major version, i.e for ID3v2.3 it is 3
 */
static TagResult tag_read_frame (int f, int version, int *carry_on,
   char **frame_id_ret, unsigned char **data_ret, int *total_bytes, 
   TagData *tag_data)
{
  unsigned char buff[20];
  unsigned char frameId[5]; // leave room for a \0
  unsigned char b1, b2, b3, b4; 
  int frame_len = 0;
  int header_len = 0;
  *frame_id_ret = NULL;
  *data_ret = NULL;

  memset (frameId, 0, sizeof (frameId));

  if (version >= 3)
  {
    // v2.3 has 4 byte ID and 4 byte size, 2 byte flags. Total
    //  frame header size is 10 
    header_len = 10;

    if (read (f, frameId, 4) != 4) return TAG_TRUNCATED; 

    if (frameId[0] == 0)
    {
      if (tag_debug)
        printf ("Got a null frame ID in v2.3 header\n");

      *carry_on = 0; // We've hit something we can't process, but
                   //  previous data should be OK
      return TAG_OK;
    }

    if (tag_debug)
      printf ("Found frame of type %s\n", frameId);

    if (read (f, buff, 6) != 6) return TAG_TRUNCATED; 
    b1 = buff[0];
    b2 = buff[1];
    b3 = buff[2];
    b4 = buff[3];

    // It seems the 2.4 and above use syncsafe lengths in both
    //  the header and the fields, while earlier versions use
    //  then only in the header. This is not clear from the specifications.

    if (version > 3)
      {
      // Is this (use of 7bit fields) all we really need to do
      //  to support sync-safe integers
      frame_len = (128 * 128 * 128) * b1 + 
        (128 * 128) * b2 + 
        (128) * b3 + 
         b4;
      }
    else
      {
      frame_len = (256 * 256 * 256) * b1 + 
        (256 * 256) * b2 + 
        (256) * b3 + 
        b4;
      }
  }
  else
  {
    // v2.2 has 3 byte ID and 3 byte size, no flags. Total
    //  frame header size is 6
    header_len = 6;

    if (read (f, frameId, 3) != 3) return TAG_TRUNCATED; 

    if (frameId[0] == 0)
    {
      if (tag_debug)
        printf ("Got a null frame ID in v2.2 header\n");

      *carry_on = 0; // We've hit something we can't process, but
                   //  previous data should be OK
      return TAG_OK;
    }

    if (tag_debug)
      printf ("Found frame of type %s\n", frameId);

    if (read (f, buff, 3) != 3) return TAG_TRUNCATED; 
    b2 = buff[0];
    b3 = buff[1];
    b4 = buff[2];

    frame_len = (256 * 256) * b2 + 
      (256) * b3 + 
      b4;
  }

  if (tag_debug)
    printf ("Frame length = %d\n", frame_len);

  if (frame_len < 1)
    return TAG_TRUNCATED; // Out-of-spec frame

  // Ugh... easytag writes UTF-8 tags without the terminating zero, in
  // defiance of the spec. So we need to allocate one byte bigger and
  // null it. :/
  unsigned char* bigbuff = (unsigned char *) malloc (frame_len + 1); 
  if (!bigbuff) return TAG_OUTOFMEMORY;
  memset (bigbuff, 0, frame_len + 1); 

  if (read (f, bigbuff, frame_len) != frame_len)
  {
    free (bigbuff); 
    return TAG_TRUNCATED;
  }

  *total_bytes += frame_len + header_len;
    
  if (frameId[0] == 'T')
  {
    // This is a text frame
    int encoding = bigbuff[0];
    char *text = NULL; // This is where decoded text will end up
  
    char *text_start = (char *)bigbuff;

    if (encoding == 0)
    {
      // ISO-8859-1 string, starts after this byte
      text_start = (char *)bigbuff + 1;
      if (tag_debug)
        printf ("Text frame is ISO-8859-1\n");

      text = (char *)tag_convert_iso8859_to_utf8 ((const unsigned char *)
         text_start, frame_len - 1);
    }
    else if (encoding == 1)
    {
      // UTF-16 with BOM
      if (tag_debug)
        printf ("Text frame is UTF-16 with BOM\n");

      text_start = (char *)bigbuff + 1;
      text = tag_convert_utf16_to_utf8 (1, (const UTF16 *)text_start, 
        frame_len - 1); 
    }
    else if (encoding == 2)
    {
      // UTF-16 without BOM
      if (tag_debug)
        printf ("Text frame is UTF-16E without BOM\n");

      text_start = (char *)bigbuff + 1;
      text = tag_convert_utf16_to_utf8 (0, (const UTF16 *)text_start, 
        frame_len - 1); 
    }
    else if (encoding == 3)
    {
      if (tag_debug)
        printf ("UTF-8 encoding\n");

      text_start = (char *)bigbuff;
      text = strdup (text_start + 1);
    }
  else
    {
      if (tag_debug)
        printf ("No encoding -- assuming ISO-8859-1\n");
      text_start = (char *)bigbuff;
      text = (char *)tag_convert_iso8859_to_utf8 ((const unsigned char *)
        text_start, frame_len);
    }

  if (text)
    {
    *frame_id_ret = strdup ((char *)frameId);
    *data_ret = (unsigned char *)text;
    }
  }
  else if (frameId[0] == 'A' && frameId[1] == 'P' && 
     frameId[2] == 'I' && frameId[3] == 'C')
  {
    if (bigbuff[0] == 0)
    {
      char mime_type[100];
      strncpy (mime_type, (char *)bigbuff + 1, 100);
      if (tag_debug)
        printf ("Picture MIME %s\n", mime_type);

      char *p = (char *)bigbuff + 1 + strlen (mime_type) + 1;
      int type = (int) (*p);
      if (tag_debug)
        printf ("Picture type %d\n", (int) (*p));
      if (type == 3) // Front cover
      {
        while (*p++); // Skip to end of pic description
        // p now points to the start of the image data
        int offset = p - (char *)bigbuff;
        int to_read = frame_len - offset;
        tag_data->cover = (unsigned char *) malloc (to_read);
        if (tag_data->cover)
        {
        memcpy (tag_data->cover, p, to_read);
        tag_data->cover_len = to_read;
        strncpy (tag_data->cover_mime, mime_type, 
          sizeof (tag_data->cover_mime));
        }
      }
    }
  }
  else if (strncmp ((char *)frameId, "COMM", 4) == 0)
  {
    //NOTE
    //  We assume that the 'short' comment is missing -- there will just be 
    //   a null (0, or 0,0) for that entry. Thus the real comment starts
    //   a fixed offset from the tag start. We also assume only one
    //   language (lanugage code is not stored or reported)
    if (tag_debug)
      printf ("Found comment tag\n");

    int encoding = bigbuff[0];
    char *text = NULL; // This is where decoded text will end up
    char *text_start = (char *)bigbuff;

    if (bigbuff[4] == 0)
    {
      if (encoding == 0)
      {
        // ISO-8859-1 string, starts after this byte
        text_start = (char *)bigbuff + 5;
        if (tag_debug)
          printf ("Text frame is ISO-8859-1\n");

        text = (char *)tag_convert_iso8859_to_utf8 ((const unsigned char *)
           text_start, frame_len - 5);
      }
      else if (encoding == 1)
      {
        // UTF-16 with BOM
        if (tag_debug)
          printf ("Text frame is UTF-16 with BOM\n");

        text_start = (char *)bigbuff + 8;
        text = tag_convert_utf16_to_utf8 (1, (const UTF16 *)text_start, 
          frame_len - 8); 
      }
      else if (encoding == 2)
      {
        // UTF-16 without BOM
        if (tag_debug)
          printf ("Text frame is UTF-16E without BOM\n");

        text_start = (char *)bigbuff + 6; 
        text = tag_convert_utf16_to_utf8 (0, (const UTF16 *)text_start, 
          frame_len - 6); 
      }
      else if (encoding == 3)
      {
        if (tag_debug)
          printf ("UTF-8 encoding\n");

        text_start = (char *)bigbuff;
        text = strdup (text_start + 5); 
      }
    else
      {
        if (tag_debug)
          printf ("No encoding -- assuming ISO-8859-1\n");
        text_start = (char *)bigbuff;
        text = (char *)tag_convert_iso8859_to_utf8 ((const unsigned char *)
          text_start, frame_len);
      }
    }

    if (text)
    {
      *frame_id_ret = strdup ((char *)frameId);
      *data_ret = (unsigned char *)text;
    }
  }
  else
  {
    // We only handle text, comment + APIC frames at present
  }

  free (bigbuff); // FRIG
  *carry_on = 1; // Should be OK to read next frame
  return TAG_OK;
}

/*
 * Caller should not assume that tag_data has not been populated just
 * because this function returns an error. Call tag_free_tag_data()
 * anyway
 */
TagResult tag_get_id3v2_tags (const char *file, TagData **tag_data_ret)
{
  TagData *tag_data = (TagData*) malloc (sizeof (TagData));
  if (!tag_data)
    {
    *tag_data_ret = NULL; 
    return TAG_OUTOFMEMORY;
    }

  *tag_data_ret = tag_data; 
  memset (tag_data, 0, sizeof (TagData));

  int f = open (file, O_RDONLY | O_BINARY);
  char buff[10];
  unsigned char b1, b2, b3, b4; 

  if (f <= 0) return TAG_READERROR;

  if (read (f, &buff, 10) != 10)
    {
    close (f);
    return TAG_NOID3V2;
    }

  if (strncmp (buff, "ID3", 3))
    {
    close (f);
    return TAG_NOID3V2;
    }

  int id3Major = buff[3];
  int id3Minor = buff[4];

  if (tag_debug)
    printf ("ID3v2 version = %d.%d\n", id3Major, id3Minor);

  if (buff[5] & 0x80)
    {
    // We don't support extended headers yet
    close (f);
    return TAG_UNSUPFORMAT;
    }

  b1 = buff[6];
  b2 = buff[7];
  b3 = buff[8];
  b4 = buff[9];

  int id3len = (128 * 128 * 128) * b1 + 
    (128 * 128) * b2 + 
    (128) * b3 + 
    b4;

  if (tag_debug)
    printf ("ID3V2 Header length = %d\n", id3len);

  TagResult r;
  int carry_on = 1;
  Tag **p_current_tag = &(tag_data->tag); 
  int total_bytes = 0;
  do
    {
    char *frameId = NULL;
    unsigned char *data = NULL;
    r = tag_read_frame (f, id3Major, &carry_on, &frameId, &data, &total_bytes,
      tag_data); // We pass tag_data here only for the APIC frame

    if (tag_debug)
      printf ("Read %d bytes from header\n", total_bytes);

    if (frameId && data)
      {
      Tag *tag = (Tag *)malloc (sizeof (Tag));
      memset (tag, 0, sizeof (Tag)); 
      tag->frameId = frameId;
      tag->data = data;
      tag->next = NULL;
      *p_current_tag = tag; 
      p_current_tag = &((*p_current_tag)->next);
      }
    } while (r == TAG_OK && carry_on && total_bytes < id3len);
 
  if (tag_debug)
    printf ("Read %d bytes from header\n", total_bytes);

  close (f);
  return r;
}


/**********************************************************************
  FLAC/VORBIS SUPPORT 
*********************************************************************/

TagResult tag_parse_vorbis_comments (const unsigned char *buff, 
   Tag **p_current_tag)
{
  // Might as well be thorough, but it's unlikely the vendor string
  //  will be megabytes long :)
  int vend_size = buff[0] + buff[1] * 256 + buff[2] * 256 * 256
    + buff[3] * 256 * 256 * 256;

  // Note that sizes in Vorbis comments are little-endian, unlike
  //  in ID3

  const unsigned char *p = buff + vend_size + 4;

//printf ("%2x %2x %2x %2x\n", p[0], p[1], p[2], p[3]);

  int num_comments = p[0] + p[1] * 256 + p[2] * 256 * 256
    + p[3] * 256 * 256 * 256;

  p += 4;

  if (tag_debug)
    printf ("Block contains %d comments\n", num_comments);

  int i;
  for (i = 0; i < num_comments; i++)
  {
  int comment_length = p[0] + p[1] * 256 + p[2] * 256 * 256
    + p[3] * 256 * 256 * 256;

  p += 4;

  unsigned char *temp = (unsigned char *) malloc (comment_length + 5);
  
  strncpy ((char *)temp, (char *)p, comment_length);
  temp[comment_length] = 0; 
 
  char *r = strchr ((char *)temp, '=');
  if (r)
  {
    *r = 0;
    char *frameId = strdup ((char *)temp);
    unsigned char *data = (unsigned char *) strdup (r+1);

    if (tag_debug)
      printf ("key=%s, value=%s\n", frameId, data);

    if (frameId && data)
    {
      Tag *tag = (Tag *)malloc (sizeof (Tag));
      memset (tag, 0, sizeof (Tag)); 
      tag->frameId = frameId;
      tag->data = data;
      tag->next = NULL;
      *p_current_tag = tag; 
      p_current_tag = &((*p_current_tag)->next);
    }
  }

  free (temp);
  p += comment_length;
  }

  return TAG_OK;
}


TagResult tag_get_flac_tags (const char *file, TagData **tag_data_ret)
{
  TagData *tag_data = (TagData*) malloc (sizeof (TagData));
  if (!tag_data)
    {
    *tag_data_ret = NULL; 
    return TAG_OUTOFMEMORY;
    }

  *tag_data_ret = tag_data; 
  memset (tag_data, 0, sizeof (TagData));

  int f = open (file, O_RDONLY | O_BINARY);
  if (f <= 0) return TAG_READERROR;

  unsigned char buff[100];

  if (read (f, buff, 4) != 4)
  {
    close (f);
    return TAG_UNSUPFORMAT;
  };

  if (strncmp ((char *)buff, "fLaC", 4))
  {
    close (f);
    return TAG_NOVORBIS;
  };

  BOOL got_it = FALSE;
  BOOL last_block = FALSE; 

  while (!got_it && !last_block)
  {
    if (read (f, buff, 4) != 4)
    {
      close (f);
      return TAG_NOVORBIS;
    }

    int block_type = buff[0] & 0x7F;
    last_block = buff[0] & 0x80;
    int block_size = (256 * 256) * buff[1]
       + 256 * buff[2] + buff[3];
 
    //  printf ("size = %d, last = %d type = %d\n", block_size, 
    //   last_block, block_type);

    if (block_type == 4)
    {
      if (tag_debug)
        printf ("Found comment block of size %d\n", block_size);

      got_it = 1;

      unsigned char *bigbuff = (unsigned char *) malloc (block_size);

      if (!bigbuff)
      {
        close (f);
        return TAG_OUTOFMEMORY;
      }
    
      if (read (f, bigbuff, block_size) != block_size)
      {
        close (f);
        return TAG_TRUNCATED;
      }
    
      Tag **p_current_tag = &(tag_data->tag); 
      int ret = tag_parse_vorbis_comments (bigbuff, p_current_tag);
      
      free (bigbuff); 
      close (f);
      return ret;
    }
  else
    lseek (f, block_size, SEEK_CUR);
  }

  close (f);
  return TAG_OK;
}


TagResult tag_get_ogg_tags (const char *file, TagData **tag_data_ret)
{
  int f = open (file, O_RDONLY | O_BINARY);
  if (f <= 0) return TAG_READERROR;

  unsigned char buff[100];

  if (read (f, buff, 4) != 4)
  {
    close (f);
    return TAG_UNSUPFORMAT;
  };

  if (strncmp ((char *)buff, "OggS", 4))
  {
    close (f);
    return TAG_NOVORBIS;
  };

  if (tag_debug)
    printf ("Found Ogg marker\n");

  int page_start = 0;
  lseek (f, page_start + 26, SEEK_SET);
  read (f, buff, 1);
  int segments = buff[0];
  
  int i;
  int total_seg_size = 0;
  for (i = 0; i < segments; i++)
    {
    lseek (f, page_start + 27 + i, SEEK_SET);
    read (f, buff, 1);
    int seg_size = buff[0];
    total_seg_size += seg_size;
    }

   int page_size = 27 + segments + total_seg_size;
   if (tag_debug)
       printf ("Ogg page size is %d\n", page_size);

   lseek (f, page_start + page_size, SEEK_SET);
   read (f, buff, 4);
   if (strncmp ((char *)buff, "OggS", 4))
     {
     if (tag_debug)
       printf ("Page length offset does not indicate next page\n");
     return TAG_NOVORBIS;
     }

  page_start = page_start + page_size;
  lseek (f, page_start + 26, SEEK_SET);
  read (f, buff, 1);
  segments = buff[0];
  lseek (f, page_start + 27 + segments + 7, SEEK_SET);

  TagData *tag_data = (TagData*) malloc (sizeof (TagData));
  if (!tag_data)
    {
    *tag_data_ret = NULL; 
    return TAG_OUTOFMEMORY;
    }

  *tag_data_ret = tag_data; 
  memset (tag_data, 0, sizeof (TagData));

  // Memory is cheap, especially if temporary. Need to be sure to capture
  //  all the comments, but it doesn't matter if we read too much
  int bigbuff_size = 4096;
  char *bigbuff = malloc (bigbuff_size);
  memset (bigbuff, 0, bigbuff_size);
  read (f, bigbuff, bigbuff_size);

  Tag **p_current_tag = &(tag_data->tag); 
  int ret = tag_parse_vorbis_comments ((unsigned char *)bigbuff, p_current_tag);

  close (f);

  return ret;
}


/**********************************************************************
  QuickTime/MP4/M4A/M4B SUPPORT 
*********************************************************************/

static int tag_mp4_decode_32_bit_msb (const BYTE *s)
  {
  return (s[0] << 24) + (s[1] << 16) + (s[2] << 8) + s[3];
  }


void tag_mp4_parse_ilst (const BYTE *ilist, int l, TagData *tag_data)
  {
  if (tag_debug)
    printf ("Found MP4 ilst atom\n");
	
  const BYTE *p = ilist;
  p += 0;
  while (p - ilist < l)
    {
    int ll = tag_mp4_decode_32_bit_msb (p);
    const BYTE *type = p + 4;
    const BYTE *dlen = p + 8;
    int data_len = tag_mp4_decode_32_bit_msb (dlen);
    const BYTE *dtype = p + 16;
    int data_type = tag_mp4_decode_32_bit_msb (dtype);
    const BYTE *data = p + 24;

    if (data_type == 1) // text
      {
      char tag_name[5];
      if (type[0] == 0xA9)
        { strncpy (tag_name, (char*)type+1, 3); tag_name[3] = 0; }
      else
        { strncpy (tag_name, (char*)type, 4); tag_name[4] = 0; }
      if (tag_debug) printf ("Text tag: name=%s, value=%s\n", tag_name, data);  
      Tag *p_tag = tag_data->tag; 
      Tag *tag = malloc (sizeof (Tag));
      tag->frameId = strdup (tag_name);
      tag->type = TAG_TYPE_TEXT;
      tag->data = (unsigned char*) strdup ((char *)data);
      tag->next = p_tag;
      tag_data->tag = tag;
      }
    else // The only non-text we handle is the cover image 
      {
      if (strncmp ((char*)type, "covr", 4) == 0)
        {
        tag_data->cover = (unsigned char *) malloc (data_len);
        memcpy (tag_data->cover, data, data_len);
        tag_data->cover_len = data_len;
        if (data_type == 13)
          strcpy (tag_data->cover_mime, "image/jpeg");
        else
          strcpy (tag_data->cover_mime, "image/png");
        }
      }
    p += ll;
    }
  }


void tag_mp4_parse_meta (const BYTE *meta, int l, TagData *tag_data)
  {
  if (tag_debug)
    printf ("Found MP4 meta atom\n");
	
  const BYTE *p = meta;
  p += 4;
  while (p - meta < l)
    {
    int ll = tag_mp4_decode_32_bit_msb (p);
    const BYTE *type = p + 4;
    if (strncmp ((char *)type, "ilst", 4) == 0)
      {
      tag_mp4_parse_ilst (p + 8, ll - 8, tag_data);
      }
    p += ll;
    }
  }


void tag_mp4_parse_udta (const BYTE *udta, int l, TagData *tag_data)
  {
  if (tag_debug)
    printf ("Found MP4 udta atom\n");
	
  const BYTE *p = udta;
  while (p - udta < l)
    {
    int ll = tag_mp4_decode_32_bit_msb (p);
    const BYTE *type = p + 4;
    if (strncmp ((char *)type, "meta", 4) == 0)
      {
      tag_mp4_parse_meta (p + 8, ll - 8, tag_data);
      }
    p += ll;
    }
  }


void tag_mp4_parse_moov (const BYTE *moov, int l, TagData *tag_data)
  {
  const BYTE *p = moov;
  while (p - moov < l)
    {
    int ll = tag_mp4_decode_32_bit_msb (p);
    const BYTE *type = p + 4;
   if (strncmp ((char *)type, "udta", 4) == 0)
     {
     tag_mp4_parse_udta (p + 8, ll - 8, tag_data);
     }
    p += ll;
    }
  }



TagResult tag_get_mp4_tags (const char *file, TagData **tag_data_ret)
  {
  TagData *tag_data = (TagData*) malloc (sizeof (TagData));
  if (!tag_data)
    {
    *tag_data_ret = NULL; 
    return TAG_OUTOFMEMORY;
    }

  *tag_data_ret = tag_data; 
  memset (tag_data, 0, sizeof (TagData));

  int f = open (file, O_RDONLY | O_BINARY);
  if (f <= 0) return TAG_READERROR;

  BOOL done = FALSE;
  while (!done)
    {
    BYTE buff[4];
    int n = read (f, buff, 4);
    if (n == 4)
      {
      int l = tag_mp4_decode_32_bit_msb (buff);
 
      int n = read (f, buff, 4);
      if (n == 4)
        {
        BOOL read_atom = FALSE;
        if (strncmp ((char *)buff, "moov", 4) == 0)
          {
          if (tag_debug)
            printf ("Found MP3 moov atom\n");
          BYTE *atom = malloc (l - 8 + 1);
          int n = read (f, atom, l - 8);
          if (n == l - 8)
            {
            read_atom = TRUE;
            tag_mp4_parse_moov (atom, l - 8, tag_data);
            }
          else
            done = TRUE;
          free (atom);
          }
        if (!read_atom)
          lseek (f, l - 8, SEEK_CUR);
        }
      else
        {
        done = TRUE;
        if (tag_debug) printf ("Unexpected end of MP4 file\n"); 
        }
      }
   else 
     {
     done = TRUE;
     if (tag_debug) printf ("Reached end of MP4 file\n"); 
     }
   }

  close (f);
  return TAG_OK;
  }


/**********************************************************************
  TAG STRUCT HANDLING 
*********************************************************************/

/*
 * Returns the number of tags in the tag_data structure, if any
 */
int tag_get_tag_count (TagData *tag_data)
{
  if (!tag_data) return 0;
  int count = 0;
  Tag *t = tag_data->tag;
  while (t)
  {
    count++;
    t = t->next;
  }
  return count;
}

/*
 * Frees the data collected by tag_get_id3v2_tags()
 * It doesn't hurt to call the function more than once, or even
 * in circumstances where get_get_id3v2_tags() failed.
  */
void tag_free_tag_data (TagData *tag_data)
{
  if (!tag_data) return;
  Tag *t = tag_data->tag;
  while (t)
  {
    free (t->frameId);
    free (t->data);
    Tag *tt = t;
    t = t->next;
    free (tt);
  }
  if (tag_data->cover) free (tag_data->cover);
  free (tag_data);
}


/*
 * Returns a Tag structure with references to data representing a specific
 * tag in the tag_data list. Note that these references are to internal
 * elements of the implementation, and must not be modified or freed
 * by the caller. Results are undefined if index is out of bounds
 */

Tag *tag_get_tag (const TagData *tag_data, int index) 
{
  if (!tag_data) return NULL;
  int count = 0;
  Tag *t = tag_data->tag;
  while (t)
  {
    if (count == index)
    {
      return t;
    }
    count++;
    t = t->next;
  }
  return NULL;
}


/*
 *  Gets a tag's text from the frame ID. Note that frame IDs are
 *  not the same in different revisions of the ID3v2 spec :/
 *  Or in FLAC. And note also that FLAC (Vorbis) tags can be of
 *  mixed case
 */
const unsigned char *tag_get_by_id (const TagData *tag_data, const char *id)
{
  Tag *t = tag_data->tag;
  while (t)
  {
    if (strcasecmp (t->frameId, id) == 0)
    {
      return t->data;
    }
    t = t->next;
  }
  return NULL;
}

/*
 * Gets a tag's text from the ID which is common to all of v2.2-v.24
 * field IDs. It is irritating that these IDs are not the same across
 * all versions :/ Returns NULL if the tag is not found. The caller must
 * not modify or free the returned string. Note that TAG_COMMON_YEAR and
 * TAG_COMMON_DATE return the same data -- the DATE label was introduced
 * when gettags got support for MP4, which includes full dates rather
 * that just years.
 *
 * All results are in UTF-8 encoding
 */
const unsigned char *tag_get_common (const TagData *tag_data, TagCommonID id)
{
  const unsigned char *s;
  switch (id)
  {
  case TAG_COMMON_TITLE:
    s = tag_get_by_id (tag_data, "TIT2");
    if (!s) s = tag_get_by_id (tag_data, "TT2");
    if (!s) s = tag_get_by_id (tag_data, "TITLE");
    if (!s) s = tag_get_by_id (tag_data, "nam");
    return s;
  case TAG_COMMON_ARTIST:
    s = tag_get_by_id (tag_data, "TPE1");
    if (!s) s = tag_get_by_id (tag_data, "TP1");
    if (!s) s = tag_get_by_id (tag_data, "ARTIST");
    if (!s) s = tag_get_by_id (tag_data, "PERFORMER");
    if (!s) s = tag_get_by_id (tag_data, "ART");
    return s;
  case TAG_COMMON_ALBUM_ARTIST:
    s = tag_get_by_id (tag_data, "TPE2");
    if (!s) s = tag_get_by_id (tag_data, "TP2");
    if (!s) s = tag_get_by_id (tag_data, "ALBUMARTIST");
    if (!s) s = tag_get_by_id (tag_data, "aART");
    return s;
  case TAG_COMMON_GENRE:
    s = tag_get_by_id (tag_data, "TCON");
    if (!s) s = tag_get_by_id (tag_data, "TCO");
    if (!s) s = tag_get_by_id (tag_data, "GENRE");
    if (!s) s = tag_get_by_id (tag_data, "gen");
    if (!s) s = tag_get_by_id (tag_data, "gnre");
    return s;
  case TAG_COMMON_ALBUM:
    s = tag_get_by_id (tag_data, "TALB");
    if (!s) s = tag_get_by_id (tag_data, "TAL");
    if (!s) s = tag_get_by_id (tag_data, "ALBUM");
    if (!s) s = tag_get_by_id (tag_data, "ALBUM");
    if (!s) s = tag_get_by_id (tag_data, "alb");
    return s;
  case TAG_COMMON_COMPOSER:
    s = tag_get_by_id (tag_data, "TCOM");
    if (!s) s = tag_get_by_id (tag_data, "TCM");
    if (!s) s = tag_get_by_id (tag_data, "COMPOSER");
    if (!s) s = tag_get_by_id (tag_data, "wrt");
    return s;
  case TAG_COMMON_YEAR:
  case TAG_COMMON_DATE:
    s = tag_get_by_id (tag_data, "TYER");
    if (!s) s = tag_get_by_id (tag_data, "TYE");
    if (!s) s = tag_get_by_id (tag_data, "DATE");
    if (!s) s = tag_get_by_id (tag_data, "day");
    return s;
  case TAG_COMMON_TRACK:
    s = tag_get_by_id (tag_data, "TRCK");
    if (!s) s = tag_get_by_id (tag_data, "TRK");
    if (!s) s = tag_get_by_id (tag_data, "TRACKNUMBER");
    if (!s) s = tag_get_by_id (tag_data, "trkn");
    return s;
  case TAG_COMMON_COMMENT:
    s = tag_get_by_id (tag_data, "COMM");
    if (!s) s = tag_get_by_id (tag_data, "COM");
    if (!s) s = tag_get_by_id (tag_data, "DESCRIPTION");
    if (!s) s = tag_get_by_id (tag_data, "COMMENT");
    if (!s) s = tag_get_by_id (tag_data, "cmt");
    return s;
  }
  return NULL;
}


TagResult tag_get_tags (const char *file, TagData **tag_data_ret)
{
  TagResult ret = tag_get_id3v2_tags (file, tag_data_ret);
  if (ret == TAG_NOID3V2)
  {
    tag_free_tag_data (*tag_data_ret);
    ret = tag_get_flac_tags (file, tag_data_ret);
    if (ret == TAG_NOVORBIS)
    {
      tag_free_tag_data (*tag_data_ret);
      ret = tag_get_ogg_tags (file, tag_data_ret);
      if (ret == TAG_NOVORBIS)
      {
        tag_free_tag_data (*tag_data_ret);
        ret = tag_get_mp4_tags (file, tag_data_ret);
        if (ret == TAG_NOMP4)
	  ret = TAG_UNSUPFORMAT;
      }
    }
  }
  return ret;
}


