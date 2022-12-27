#!/bin/bash
# Update by Eduardo Fraga <eduardo@eftech.com.br> at 27 Dec 2022
# 
#
# Required: git

# Required environment variables above: 
if [ -z $USERNAME ] || [ -z $TOKEN ] || [ -z $ORG ]; then
  echo "Missing environmet variables USERNAME, TOKEN, ORG"
  exit 1; 
fi

# Args
if [ -z $1 ] || [ -z $2 ]; then
  echo "Required $0 <repo> <destdir>"
  exit 2
fi

DIR=`mktemp -d`

cd $DIR

git clone https://$USERNAME:$TOKEN@github.com/$ORG/$1

cd $1

git bundle create $2 --all

git bundle verify $2 || exit 3

rm -rf $DIR
