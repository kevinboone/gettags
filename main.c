/*==========================================================================
gettags
main.c
Copyright (c)2012-2018 Kevin Boone
Distributed under the terms of the GNU Public Licence, v3.0
==========================================================================*/
  
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include "types.h"
#include "tag_reader.h"

/**
print_short_usage
Prints a short usage message
*/
void print_short_usage(const char *argv0)
  {
  printf ("Usage: %s -[vhds] [-c name] [-e name] {files...}\n", argv0);
  printf ("\"%s --longhelp\" for full details\n", argv0);
  }


/**
print_long_usage
Prints a detailed usage message
*/
void print_long_usage(const char *argv0)
  {
  printf ("Usage: %s [options]\n", argv0);
  printf ("-c, --common-name [name] show tag matching only this common name\n");
  printf ("-C, --common-only        show only common tags\n");
  printf ("-c help                  lists common names\n");
  printf ("-d, --debug              show debugging data\n");
  printf ("-e, --exact-name [name]  show tag matching only this exact name\n");
  printf ("--longhelp               show detailed usage\n");
  printf ("-h, --help               show brief usage\n");
  printf ("-o, --cover_filename     extract cover image\n");
  printf ("-s, --script             script mode\n");
  printf ("-v, --version            show version\n");
  }


/**
show_tag
prints a tag if its type is text
*/
void show_tag (const Tag *tag)
  {
  printf ("%s", tag->frameId);
  printf (" ");
  if (tag->type == TAG_TYPE_TEXT)
    printf ("%s", (char*)tag->data);
  else
    printf ("(binary)");
  printf ("\n");
  }


/*
 * make_prefix
 * Helper function to write the OK/ERROR prefix when in script mode
 */
const char *make_prefix (BOOL ok, BOOL script)
  {
  if (!script) return "";
  return ok ? "OK " : "ERROR ";
  }


/**
 * get_ext_from_mime
 * Get a filename extension appropriate for the specified image mimetype.
 * We don't need to be exhaustive -- in practice only JPEG and PNG are used
 */
const char *get_ext_from_mime (const char *mime)
  {
  if (strcasecmp (mime, "image/jpeg") == 0) return "jpg";
  if (strcasecmp (mime, "image/png") == 0) return "png";
  if (strcasecmp (mime, "image/gif") == 0) return "gif";
  return NULL;
  }


/*
 * extract_cover
 * Writes the cover image, if any, to a file whose name is made from
 * the specified filename, plus an extension appropriate for the 
 * type of the cover image.
 */
void extract_cover (const char *argv0, const TagData *tag_data, 
    const char *cover_filename, BOOL script)
  {
  if (tag_data->cover)
    {
    if (tag_data->cover_mime[0])
      {
      char full_filename[512];
      const char *ext = get_ext_from_mime (tag_data->cover_mime);
      snprintf (full_filename, sizeof (full_filename),
         "%s.%s", cover_filename, ext);
      int f = open (full_filename, O_WRONLY | O_TRUNC | O_CREAT, 0xFFFF);
      if (f > 0)
        {
        write (f, tag_data->cover, tag_data->cover_len);
        close (f);
        }
      else
        {
        printf ("%s%s: can't open file for writing: %s (%s)\n", 
          make_prefix (FALSE, script), argv0, cover_filename, strerror (errno));
        }
      }
    else
      {
      printf ("%s%s: cover image found, but file type is unknown\n", 
        make_prefix (FALSE, script), argv0);
      }
    }
  else 
    printf ("%s%s: no cover image found\n", 
      make_prefix (FALSE, script), argv0);
  }


/**
do_file
Process a file, according to the specified command-line arguments
*/
void do_file (const char *argv0, const char *filename, BOOL script, 
    TagCommonID common_id, const char *exact_name, BOOL common_only,
      const char *cover_filename)
  {
  TagData *tag_data = NULL; 
  TagResult r = tag_get_tags (filename, &tag_data);
  switch (r)
    {
    case TAG_READERROR: 
      fprintf (stderr, "%s%s: Can't read file '%s'\n", 
        make_prefix(FALSE, script), argv0, filename);
      break;
    case TAG_TRUNCATED:
      fprintf (stderr, "%s%s, Tag data is incomplete in '%s'\n", 
        make_prefix(FALSE, script), argv0, filename);
      break;
    case TAG_OUTOFMEMORY:
      fprintf (stderr, "%s%s: Out of memory processing file '%s'\n", 
        make_prefix(FALSE, script), argv0, filename);
      break;
    case TAG_UNSUPFORMAT:
    case TAG_NOID3V2:
    case TAG_NOVORBIS:
      fprintf (stderr, "%s%s: Unsupported tag format or no tags in file "
       "'%s'\n", 
        make_prefix(FALSE, script), argv0, filename);
      break;
    case TAG_OK:
      {
      // Only if we get here should we proceed
      if (strlen (cover_filename) > 0)
        {
        extract_cover (argv0, tag_data, cover_filename, script); 
        }
      else if (strlen (exact_name) > 0)
        {
        const char *s = (char *)tag_get_by_id (tag_data, exact_name);
        if (s)
          printf ("%s%s\n", make_prefix(TRUE, script), s);
        else
          printf ("%sTag not found\n", make_prefix(FALSE, script));
        }
      else if (common_id != -1)
        {
        const unsigned char *s = tag_get_common 
          (tag_data, common_id);
        if (s)
          printf ("%s%s\n", make_prefix(TRUE, script), s);
        else
          fprintf (stderr, "%sTag not found\n", make_prefix(FALSE, script));
        }
      else
        {
        if (script) printf ("OK\n");
        if (common_only)
          {
          const unsigned char *s = tag_get_common 
            (tag_data, TAG_COMMON_ALBUM);
          if (s) printf ("%s %s\n", "album", s);
          s = tag_get_common 
            (tag_data, TAG_COMMON_ARTIST);
          if (s) printf ("%s %s\n", "artist", s);
          s = tag_get_common 
            (tag_data, TAG_COMMON_ALBUM_ARTIST);
          if (s) printf ("%s %s\n", "album-artist", s);
          s = tag_get_common 
            (tag_data, TAG_COMMON_COMMENT);
          if (s) printf ("%s %s\n", "comment", s);
          s = tag_get_common 
            (tag_data, TAG_COMMON_COMPOSER);
          if (s) printf ("%s %s\n", "composer", s);
          s = tag_get_common 
            (tag_data, TAG_COMMON_DATE);
          if (s) printf ("%s %s\n", "date", s);
          s = tag_get_common 
            (tag_data, TAG_COMMON_GENRE);
          if (s) printf ("%s %s\n", "genre", s);
          s = tag_get_common 
            (tag_data, TAG_COMMON_TITLE);
          if (s) printf ("%s %s\n", "title", s);
          s = tag_get_common 
            (tag_data, TAG_COMMON_TRACK);
          if (s) printf ("%s %s\n", "track", s);
          }
        else
          {
          Tag *t = tag_data->tag;
          while (t)
            {
            show_tag (t);
            t = t->next;
            }
          }
        }
      }
      break;
    default:
      fprintf (stderr, "%s%s: Internal error processing file '%s'\n", 
        make_prefix(FALSE, script), argv0, filename);
    }
  tag_free_tag_data (tag_data);
  }



/**
common_name_to_common_id
Maps human-readable tag names to constants defined in the header file
*/
TagCommonID common_name_to_common_id (const char *common_name)
  {
  if (strcmp (common_name, "album") == 0)  return TAG_COMMON_ALBUM; 
  if (strcmp (common_name, "album-artist") == 0)  
    return TAG_COMMON_ALBUM_ARTIST; 
  if (strcmp (common_name, "artist") == 0)  return TAG_COMMON_ARTIST; 
  if (strcmp (common_name, "comment") == 0) return TAG_COMMON_COMMENT; 
  if (strcmp (common_name, "composer") == 0)  return TAG_COMMON_COMPOSER; 
  if (strcmp (common_name, "date") == 0) return TAG_COMMON_DATE; 
  if (strcmp (common_name, "genre") == 0)  return TAG_COMMON_GENRE; 
  if (strcmp (common_name, "title") == 0)  return TAG_COMMON_TITLE; 
  if (strcmp (common_name, "track") == 0) return TAG_COMMON_TRACK; 
  if (strcmp (common_name, "year") == 0)  return TAG_COMMON_YEAR; 
  return -1;
  }


/**
main
*/
int main (int argc, char **argv)
  {
  static BOOL opt_version = FALSE;
  static BOOL opt_help = FALSE;
  static BOOL opt_longhelp = FALSE;
  static BOOL opt_debug = FALSE;
  static BOOL opt_script = FALSE;
  static BOOL opt_common_only = FALSE;
  char opt_common_name[512];
  char opt_exact_name[32];
  char opt_cover_filename[512];

  static struct option long_options[] = 
    {
    {"help", no_argument, &opt_help, 0},
    {"longhelp", no_argument, &opt_longhelp, 0},
    {"version", no_argument, &opt_version, 'v'},
    {"script", no_argument, &opt_script, 's'},
    {"debug", no_argument, &opt_debug, 'v'},
    {"common-name", required_argument, NULL, 'c'},
    {"common-only", no_argument, NULL, 'C'},
    {"exact-name", required_argument, NULL, 'e'},
    {"cover-filename", required_argument, NULL, 'o'},
    {0, 0, 0, 0},
    };

  opt_common_name[0] = 0;
  opt_exact_name[0] = 0;
  opt_cover_filename[0] = 0;

  while (1)
    {
    int option_index = 0;
    int opt = getopt_long (argc, argv, "?vhdc:Ce:so:", long_options, 
      &option_index);
    if (opt == -1) break;
    switch (opt)
      {
      case 0: // Long option
        {
        if (strcmp (long_options[option_index].name, "longhelp") == 0)
          {
          opt_longhelp = TRUE;
          }
        else if (strcmp (long_options[option_index].name, "help") == 0)
          {
          opt_help = TRUE;
          }
        else if (strcmp (long_options[option_index].name, "debug") == 0)
          {
          opt_debug = TRUE;
          }
        else if (strcmp (long_options[option_index].name, "script") == 0)
          {
          opt_script = TRUE;
          }
        else if (strcmp (long_options[option_index].name, "common-name") == 0)
          {
          strncpy (opt_common_name, optarg, sizeof (opt_common_name));
          }
        else if (strcmp (long_options[option_index].name, 
             "cover-filename") == 0)
          {
          strncpy (opt_common_name, optarg, sizeof (opt_cover_filename));
          }
        else if (strcmp (long_options[option_index].name, "common-only") == 0)
          {
          opt_common_only = TRUE;	
          }
        else if (strcmp (long_options[option_index].name, "exact-name") == 0)
          {
          strncpy (opt_exact_name, optarg, sizeof (opt_exact_name));
          }
        } // End of long options
        break;
      case 'v':
        opt_version = TRUE;
        break;
      case 'd':
        opt_debug = TRUE;
        break;
      case 's':
        opt_script = TRUE;
        break;
      case '?': case 'h':
        opt_help = TRUE;
        break;
      case 'c': 
        strncpy (opt_common_name, optarg, sizeof (opt_common_name));
        break;
      case 'o': 
        strncpy (opt_cover_filename, optarg, sizeof (opt_cover_filename));
        break;
      case 'C': 
        opt_common_only = TRUE; 
        break;
      case 'e': 
        strncpy (opt_exact_name, optarg, sizeof (opt_exact_name));
        break;
      }
    }

  if (opt_version)
    {
    printf ("gettags version %s\nCopyright (c)2011-2018 Kevin Boone\n"
      "Distributed under the terms of the GNU Public Licence, v3.0\n", 
      VERSION);
    exit (0);
    }
  
  if (opt_help)
    {
    print_short_usage(argv[0]);
    exit (0);
    }
  
  if (opt_longhelp)
    {
    print_long_usage(argv[0]);
    exit (0);
    }

  if (opt_debug)
    tag_debug = TRUE;

  TagCommonID common_id = -1; 
  if (strlen (opt_common_name) > 0)
    {
    if (strcmp (opt_common_name, "help") == 0)
      {
      printf 
       ("%s: common names: album album-artist artist comment composer date"
          " genre title track\n", 
          argv[0]); 
      return 0;
      }
    common_id = common_name_to_common_id (opt_common_name);    
    if (common_id == -1)
      {
      fprintf (stderr, "%s: unknown common name '%s'\n", 
        argv[0], opt_common_name);
      fprintf (stderr, "'%s --common-name help' for a list\n", argv[0]);
      return -1;
      }
    }

  if (common_id != -1 && strlen (opt_exact_name) > 0)
    {
    fprintf (stderr, 
      "'%s: ignoring common name because exact name was supplied\n", argv[0]);
    }

  if (optind == argc)
    {
    fprintf (stderr, 
      "%s%s: No files specified\n", make_prefix (FALSE, opt_script), argv[0]);
    }
  else
    {
    int i;
    for (i = optind; i < argc; i++)
      {
      do_file (argv[0], argv[i], opt_script, common_id, 
        opt_exact_name, opt_common_only, opt_cover_filename);
      }
    }

  return 0;
  }


