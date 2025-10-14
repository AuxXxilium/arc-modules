#!/usr/bin/env bash

root=$(pwd)
start=${root}/modules
rm -f ${start}/modules.yml
touch ${start}/modules.yml
echo "## List of included modules" >>${start}/modules.yml
for folder in ${start}/*; do
  if test -d ${folder}; then
    echo "${folder}"
    echo "" >> ${start}/modules.yml
    path=$(echo ${folder} | rev | cut -d '/' -f-1 | rev)
    echo "### ${path}" >>${start}/modules.yml
    echo "" >>${start}/modules.yml
    # Get list of all modules
    for F in $(ls ${folder}/*.ko); do
      X=$(basename ${F})
      M=$(basename ${F} | sed 's/\.[^.]*$//')
      DESC=$(modinfo ${F} | awk -F':' '/description:/{ print $2}' | awk '{sub(/^[ ]+/,""); print}' | sed -E 's/\(Compiled by RR for DSM\)//g')
      [ -z "${DESC}" ] && DESC="${X}"
      echo "${M} \"${DESC}\""
      echo "* ${M} \"${DESC}\"" >>${start}/modules.yml
    done
  fi
done
echo "" >> ${start}/modules.yml
date=$(date +'%y.%m.%d')
echo "Update: ${date}" >>${start}/modules.yml

mv -f ${start}/modules.yml ${root}/modules.yml 