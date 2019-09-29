#!/usr/bin/env python2
# this program compare two branche of GIT
# and show the differences
import sys
import os
import logging
import collections
import re
import argparse
import time

try:
    import git
except ImportError:
    print >>sys.stderr, "you must install python-git aka GitPython"
    sys.exit(1)


def add_console_logger():
    console=logging.StreamHandler()
    console.setFormatter(logging.Formatter('%(levelname)-3.3s %(filename)s:%(lineno)d %(message)s', '%H:%M:%S'))
    console.setLevel(logging.DEBUG) # must be INFO for prod
    logging.getLogger().addHandler(console)
    return console

def add_file_logger(filename):
    filelog=logging.FileHandler(filename)
    # %(asctime)s  '%Y-%m-%d %H:%M:%S'
    filelog.setFormatter(logging.Formatter('%(asctime)s %(levelname)-3.3s %(filename)s:%(lineno)d %(message)s', '%H:%M:%S'))
    filelog.setLevel(logging.DEBUG)
    logging.getLogger().addHandler(filelog)
    return filelog



def run_cmp_branch(repo, args):
    print args.branch1
    print args.branch2
#    for commit in repo.iter_commits(args.branch1, max_count=10):
#        print commit.hexsha, commit.committed_date, commit.author.name, commit.message

#    print dir(repo)
    commons=repo.merge_base(args.branch1, args.branch2)
    if len(commons)!=1:
        print "cannot find the unique common commit between", args.branch1, args.branch2

    common=commons[0]
    # make a list of all know commit in branch-2
    commits2=set()
    for commit in repo.iter_commits(args.branch2):
        if commit.hexsha==common.hexsha:
            break

        subject=commit.message.split('\n', 1)[0]
        commits2.add((commit.authored_date, commit.author.name, subject))
        #print commit.committed_date, commit.author.name, subject

    # list and compare with commits of branch-&
    for commit in repo.iter_commits(args.branch1):
        if commit.hexsha==common.hexsha:
            break
        subject=commit.message.split('\n', 1)[0]
        date=time.strftime("%Y-%m-%d %H:%M", time.gmtime(commit.authored_date))
        if (commit.authored_date, commit.author.name, subject) in commits2:
            print "=", date, commit.author.name, subject
        else:
            print "+", date, commit.author.name, subject

mainparser=argparse.ArgumentParser(description='git utility for bacula')
subparsers=mainparser.add_subparsers(dest='command', metavar='', title='valid commands')

git_parser=argparse.ArgumentParser(add_help=False)
git_parser.add_argument('--git_dir', metavar='GIT-DIR', type=str, default='.', help='the directory with the .git sub dir')

parser=subparsers.add_parser('cmp_branch', parents=[git_parser, ], help='compare two branches, highligh commits missing in the second branch')

parser.add_argument('branch1', metavar='BRANCH-1', help='the first branch')
parser.add_argument('branch2', metavar='BRANCH-2', help='the second branch')

args=mainparser.parse_args()


logging.getLogger().setLevel(logging.DEBUG)

add_console_logger()
print args.git_dir
print "logging into gitstat.log"
add_file_logger('gitstat.log')

# search the git repo
repo=None
if args.git_dir:
    if args.git_dir=='.':
        path=os.getcwd()
        while path and not os.path.isdir(os.path.join(path, '.git')):
            path=os.path.dirname(path)
            print path

        if path and os.path.isdir(os.path.join(path, '.git')):
            try:
                repo=git.Repo(path)
            except git.exc.InvalidGitRepositoryError:
                parser.error("git repository not found in %s" % (path,))
            else:
                args.git_dir=path
        else:
            parser.error("not .git directory found above %s" % (os.getcwd(),))

    else:
        try:
            repo=git.Repo(args.git_dir)
        except git.exc.InvalidGitRepositoryError:
            parser.error("git repository not found in %s" % (args.git_dir,))

if args.command=='cmp_branch':
    run_cmp_branch(repo, args)

