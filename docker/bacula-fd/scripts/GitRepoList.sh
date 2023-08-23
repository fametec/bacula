#!/bin/bash
# Author: Sarav AK
# Email: hello@gritfy.com
# Created Date: 19 Aug 2021
# Update by Eduardo Fraga <eduardo@eftech.com.br> at 27 Dec 2022
# 
# 

if [ -z $USERNAME ] || [ -z $TOKEN ] || [ -z $ORG ]; then
  echo "Missing environmet variables USERNAME, TOKEN, ORG"
  exit 1; 
fi

# No of reposoitories per page - Maximum Limit is 100
PERPAGE=100

# Change the BASEURL to  your Org or User based
# Org base URL
BASEURL="https://api.github.com/orgs/${ORG}/repos"

# User base URL
# BASEURL="https://api.github.com/user/<your_github_username>/repos"

curl -s -u $USERNAME:$TOKEN -H 'Accept: application/vnd.github.v3+json' "${BASEURL}?per_page=${PERPAGE}&page=1" 2>&1 | jq '.[].name' | tr -d '\\"'