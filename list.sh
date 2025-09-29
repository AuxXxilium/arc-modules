#!/usr/bin/env bash

root=$(pwd)
start=${root}/modules
rm -f ${start}/modules.yml
touch ${start}/modules.yml

# Add the header for the YAML file
echo "## List of included modules" >>${start}/modules.yml

# Iterate through each folder in the modules directory
for folder in ${start}/*; do
  if test -d ${folder}; then
    # Extract the folder name and format it
    folder_name=$(basename ${folder})
    formatted_name=$(echo ${folder_name} | sed 's/-/ - /g') # Replace '-' with ' - '
    echo "" >>${start}/modules.yml
    echo "### ${formatted_name}" >>${start}/modules.yml
    echo "" >>${start}/modules.yml

    # Get the list of all modules in the folder
    for F in $(ls ${folder}/*.ko 2>/dev/null); do
      X=$(basename ${F})
      M=$(basename ${F} | sed 's/\.[^.]*$//') # Remove the file extension
      DESC=$(modinfo ${F} | awk -F':' '/description:/{ print $2 }' | awk '{sub(/^[ ]+/,""); print}' | sed 's/ (Compiled by RR for DSM)//')
      [ -z "${DESC}" ] && DESC="${X}" # Use the filename if no description is found
      echo "* ${M}: \"${DESC}\"" >>${start}/modules.yml
    done
  fi
done

# Add the update date at the end of the file
echo "" >>${start}/modules.yml
date=$(date +'%y.%m.%d')
echo "Update: ${date}" >>${start}/modules.yml

# Move the generated file to the root directory
mv -f ${start}/modules.yml ${root}/modules.yml