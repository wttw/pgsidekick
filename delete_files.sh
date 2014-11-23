#!/bin/bash

# Expects filenames on stdin, separated by ASCII NUL. Deletes any matching file in /tmp.
# For real use, change the /tmp/*) case clause to match the area of the filesystem
# you want to be able to delete files from and run as
#
# pglisten -0 | delete_files.sh

while IFS= read -r -d '' arg; do
  case "$arg" in
/tmp/*)
  [ -f "$arg" ] && rm -f "$arg"
  ;;
*)
  echo "Not deleting $arg\n" 2>&1
  ;;
  esac
done
