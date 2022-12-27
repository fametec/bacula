#!/bin/bash

# for i in `/usr/local/bin/GitRepoList.sh`; do /usr/local/bin/backup-github.sh $i /github/$i.bundle || exit 1; done

for i in `/usr/local/bin/GitRepoList.sh`; do /usr/local/bin/backup-github.sh $i /github/$i.bundle; done

