#!/usr/bin/env bash

for D in `ls -d *-*302/`; do
  E=$(echo ${D} | sed 's#/##')
  echo "${E} directory"
  for M in `ls ./${E}/*.ko`; do
    depends=`modinfo "${M}" | grep depends: | awk -F: '{print $2}' | awk '$1=$1' | sed 's/,/ /g'`
    if [ ${#depends[*]} -gt 0 ]; then
      for k in `echo "$depends"`; do
        [ ! -f "./${E}/${k}.ko" ] && echo "[W] ${M} depends ${k} does not exist" 
      done
    fi
  done
done
