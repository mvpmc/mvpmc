#!/bin/sh

cd $1

echo char compile_time[] = \"`LANG=C date`\" \; > version.c
echo char version_number[] =  \"$VERSION\"\; >> version.c
echo char build_user[] =  \"$USER\"\; >> version.c

GIT_REV_LIST=`which git-rev-list`
if [ "$GIT_REV_LIST" != "" ] ; then
    GIT_REVISION=`git-rev-list --all | head -1`
else
    GIT_REVISION=
fi
echo char git_revision[] =  \"$GIT_REVISION\"\; >> version.c
