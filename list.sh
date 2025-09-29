#!/usr/bin/env bash

root=$(pwd)
start=${root}/modules
rm -f ${start}/modules.yml
touch ${start}/modules.yml
echo "## List of included modules" >>${start}/modules.yml
for folder in ${start}/*; do
  if test -d ${folder}; then
    # Extract the folder name and format it
    folder_name=$(basename ${folder})
    formatted_name=$(echo ${folder_name} | sed 's/-/ - /g') # Replace '-' with ' - '
    echo "### ${formatted_name}" >>${start}/modules.yml
    echo "" >>${start}/modules.yml
    # Get list of all modules
    for F in $(ls ${folder}/*.ko); do
      X=$(basename ${F})
      M=$(basename ${F} | sed 's/\.[^.]*$//')
      DESC=$(modinfo ${F} | awk -F':' '/description:/{ print $2}' | awk '{sub(/^[ ]+/,""); print}' | sed 's/ (Compiled by RR for DSM)//')
      [ -z "${DESC}" ] && DESC="${X}"
      echo "${M} \"${DESC}\""
      echo "* ${M} \"${DESC}\"" >>${start}/modules.yml
    done
  fi
done
echo "" >>${start}/modules.yml
date=$(date +'%y.%m.%d')
echo "Update: ${date}" >>${start}/modules.yml

mv -f ${start}/modules.yml ${root}/modules.yml