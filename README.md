# gettags -- a simple utility for reading tags from MP3, Ogg, FLAC, M4A, and M4B files (and maybe others)</h1>

`gettags` is a simple, completely self-contained command-line 
utility for 
reading metadata tags from
audio files. At present, MP3 (ID3v2), Ogg and FLAC 
(with Vorbis-style comments), and MP4 (M4A, M4B) are supported. 
I might add other file types
if I ever accumulate enough file in those formats to make it 
worth the effort. Note that file names and extensions are irrelevant to
this utility -- `gettags` 
will try to extract tags in all the formats it
understands. Consequently, some file types may work even though I have
never tested them. 


The main purpose of gettags is to make it straightforward to write
shell or perl scripts that move music files around according to their
tags. Although there are plenty of command-line utilities that will 
read the contents of
tags, they typically work only with one type of file. gettags only extracts
a relatively small set of tags, but it does it in the same way across all
file types it supports. 


gettags is written entirely in ANSI C, with no dependencies on any other 
libraries. It is open source -- see the 
Download section below -- and can be
compiled for Linux, or for Windows using either Cygwin or MinGW. 
The complete tag reader code is contained in one (rather long and
unattractive) C source file and one header file, so it should be 
easy to incorporate the tag reader into other C/C++ applications.

## Basic usage

To display all (text) tags in a file:

```
% gettags {filename}
```

This will display tags with their "native" names. So, for example, in ID3v2
the tag that is usually interpreted as the track title is `TIT2`.
In MP4 this tag is `nam`.


To display just the title tag, if it is present:

```
% gettags -c title {filename}
```

To display the specific TALB tag in an MP3 file, if it is present:

```
% gettags -e TALB {filename}
```


## Script mode

In 'script' mode, which is enabled with the `-s` switch, all
output from gettags is prefixed with either 'OK' or 'ERROR'. This is intended
to make it easier for scripts to distinguish valid responses from error
responses. However, because all valid output is to `stdout`, and
error messages are to `stderr`, it might be easier just to
take the absence of any response on `stdout` as an indication
of an error. For examle, in perl:


```perl
my $album = `gettags -c album "$file"`;
chomp ($album);
if ($album)
  {
  # There is an album tag, so continue...
  }
else
  {
  die "No album tag in $file\n".
  }
```

## Common tags

The difference between `-e` and `-c` is significant.
`-e` specifies an exact name. The tag will only be displayed if
its exact match is found (case sensitive). However, there is no uniformity
at all between the tag naming conventions in the different file types.
In fact, there's little uniformity between tagging applications even within
the same file type, but there's not much that I can do about that.


To make it possible to work on mixed file types with the same script, 
gettags provides simple names for the various tags which are common
to most audio file formats. 
For example, the
argument `-c album` will extract a tag call 'album', 
'talb', 'alb', or 'tal',
regardless of case. 'TAL' and 'TALB' are the names used within the 
various releases of the ID3v2 specification; 'Album', or 'ALBUM' are
usually used with Ogg and FLAC files; 'alb' is used in MP4 and other
QuickTime derivatives.


The argument `-c artist` will match (regardless of case)
'TP1', 'TPE1', 'artist', 'art' or 'performer'. The latter is necessary because
there is some disagreement about how to name the primary artist in 
FLAC tags. 'album-artist' is a rather non-standard tag that seems to have
been introduced by Apple. It corresponds to the tag 'aART' in MP4/M4A/M4B,
and TPE2 in MP3. The use of TPE2 for this purpose is not particularly
widespread.


To get a list of supported common tag name, run:

```
% gettags -c help
```


## Cover art

Use the argument `-o {file\_root}` to extract a cover art image,
if there is one. Do not provide a filename extension -- the program will
write an extension appropriate to the type of the image. If multiple images
are present in the file, only the first is extracted.


## Unicode issues

ID3v2 allows for a variety of different Unicode formats, even within the
same file. Ogg, FLAC, M4A, and M4B use 8-bit characters, which in 
practice usually
means UTF-8 --  there does not seem to be any way to specify an 
encoding in any of these formats.


To unify functionality across file types, 
all output from `gettags` is encoded as UTF-8, regardless of 
the original encoding. 
This is nearly always the best way to represent text data in modern 
computer systems -- UTF-8 is widely supported, and is automatically 
detected by the majority of applications that handle text. Most importantly
from a programming/scripting perspective, UTF-8 will never contain
embedded zero bytes, so UTF-8 strings can often be handled just like
ASCII.

## Limitations

1. `gettags` can't do anything about incorrectly formatted 
tags. For example,
if the encoder says that a tag is in UTF-8 format, but actually puts
UTF-16 into the tag, that's too bad. This happens surprisingly often.

2. It's possible for badly-formatted tags to crash `gettags`. 
If you 
come across one of these, please send it to me so I can improve the 
program.

3. `gettags` only handles text tags, ID3v2 comments 
(which are a variety
of text tag), and cover art images. Any other tags that are not known to
be printable text are ignored. 

4. If there are multiple tags with the same name, all will be extracted.
This could be a problem for scripts that are expecting a single line
response.

5. `gettags` does not support ID3v1 tags, because I don't 
think anybody still
uses them.


## Example usage in a script

This example shows a simple perl script that renames a bunch of music files
according to the tags they contain. In practice, the process might be
more complicated than this, because a real script would have to deal with
issues such as illegal characters in filenames, or duplicate titles.

```perl
#!/usr/bin/perl -w

# A script that uses gettags to copy a bunch of music files to a new directory
# whose name is given by ${target_dir}/${album}/${title}/${extension}
# Where $target_dir is the first argument to the script, $title and
# $album are taken from the music file, and $extension is the original file
# extension
#
# Example:
#
# move_album.pl /media/my_albums /tmp/my_cd_rips/*.mp3

use strict;

die "Usage: move_album.pl {directory} {files...}" if (scalar(@ARGV) < 1);

my $target_dir = $ARGV[0];
print "target=", $target_dir, "\n";

system ("mkdir -p \"$target_dir\"");

for (my $i = 1; $i < scalar(@ARGV); $i++)
  {
  my $file = $ARGV[$i];
  my $album = `gettags -c album "$file"`;
  chomp ($album);
  if ($album)
    {
    my $title = `gettags -c title "$file"`;
    chomp ($title);
    if ($title)
      {
      my $ext = ($file =~ m/([^.]+)$/)[0];
      my $album_dir = "${target_dir}/${album}";
      my $target_file = "${album_dir}/${title}.${ext}";
      system ("mkdir -p \"${album_dir}\"");
      print "Copying file ", $file, "\n to ", $target_file, "\n";
      my $cmd = "cp \"$file\" \"$target_file\""; 
      system ($cmd);
      }
    }
  else
    {
    print "No album tag in '", $file, "\n";
    }
  }
```

## Bugs

If you find an MP3, FLAC, M4A, M4B, or Ogg file that gettags does not handle
correctly, please let me know.


## Legal stuff

gettags is Copyright (c)2012-2018 Kevin Boone, and distributed according to the
GNU Public Licence, version 3.0. Essentially that means that you can
do whatever you like with the code, at your own risk, provided that
the original author continues to be identified.


## Downloads

The latest source code can always be found in the
<a href="http://github.com/kevinboone/gettags">Github repository</a>.


