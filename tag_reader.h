/*==========================================================================
gettags
tag_reader.h
Copyright (c)2012-2018 Kevin Boone
Distributed under the terms of the GNU Public Licence, v3.0
==========================================================================*/
  
#pragma once

/* Error codes. Methods that read tags of a particular type should
 * return TAG_NOXXX if the file is completely uninterpretable, or contains
 * no recognizable tags. These particular error codes mean that it might
 * be worth trying again with a different format. However, errors like
 * TAG_READERROR indicate a problem with the file at the system level, while
 * TAG_TRUNCATED means that a particular tag was found, but it was 
 * defective. These errors mean that it is unlikely to be profitable to
 * try with a different reader type.
 */
typedef enum 
  {
  TAG_OK = 0,
  TAG_READERROR = 1, // File read error
  TAG_NOID3V2 = 2, // File does not contain an ID3V2 tag 
  TAG_TRUNCATED = 3, // File runs out in the middle of a frame 
  TAG_OUTOFMEMORY = 4, 
  TAG_UNSUPFORMAT = 5, // Tag is a version we don't support
  TAG_NOVORBIS = 6, // File does not contain VORBIS comments 
  TAG_NOMP4 = 7, // File does not contain MP4 metadata 
  } TagResult;

// Tag types -- but only text is supported right now
typedef enum
  {
  TAG_TYPE_TEXT = 0,
  TAG_TYPE_BINARY = 1
  } TagType;

// Common tag names -- see tag_get_common() for more information

typedef enum
  {
  TAG_COMMON_TITLE = 0,
  TAG_COMMON_ALBUM,
  TAG_COMMON_ARTIST,
  TAG_COMMON_COMPOSER,
  TAG_COMMON_YEAR,
  TAG_COMMON_GENRE,
  TAG_COMMON_TRACK,
  TAG_COMMON_COMMENT,
  TAG_COMMON_DATE,
  TAG_COMMON_ALBUM_ARTIST,
  } TagCommonID;

// Tag contains a reference to a specific tag's data
typedef struct Tag
  {
  char *frameId;
  TagType type;
  unsigned char *data;
  struct Tag *next;
  } Tag;

// TagData holds a list of tags
typedef struct
  {
  Tag *tag; // Head of a linked list of Tags  
  // Cover art data, if present
  unsigned char *cover;
  int cover_len;
  char cover_mime[30];
  } TagData;

/* NOTE: all functions that return a **tag_data_ret allocate a structure
 * in which to store the tags. This structure will be left for the caller
 * to free, regardless of whether the function found any tags or not. It
 * is crucical that the caller call tag_free_tag_data() on this structure
 * after return, regardless of outcome. */
TagResult            tag_get_id3v2_tags 
                        (const char *file, TagData **tag_data_ret);
TagResult            tag_get_ogg_tags 
                        (const char *file, TagData **tag_data_ret);
TagResult            tag_get_flac_tags 
                        (const char *file, TagData **tag_data_ret);
int                  tag_get_tag_count (TagData *tag_data);
void                 tag_free_tag_data (TagData *tag_data);
Tag                 *tag_get_tag (const TagData *tag_data, int index);
const unsigned char *tag_get_by_id (const TagData *tag_data, const char *id);
const unsigned char *tag_get_common (const TagData *tag_data, TagCommonID id);
TagResult            tag_get_tags (const char *file, TagData **tag_data_ret);

// Set tag_debug for copious debugging output
extern BOOL tag_debug;


