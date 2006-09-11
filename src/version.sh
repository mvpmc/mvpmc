#!/bin/sh

cd $1

echo char compile_time[] = \"`LANG=C date`\" \; > "$2/version.c"
echo char version_number[] =  \"$VERSION\"\; >> "$2/version.c"
echo char build_user[] =  \"$USER\"\; >> "$2/version.c"

GIT_REV_LIST=`which git-rev-list`
if [ "$GIT_REV_LIST" != "" ] ; then
    GIT_REVISION=`git-rev-list --all | head -1`
    if [ `git diff $GIT_REVISION | grep "^[+-]" | head -1 | wc -l` -ne 0 ]; then
	GIT_REVISION="$GIT_REVISION with local changes"
    fi
else
    GIT_REVISION=
fi
echo char git_revision[] =  \"$GIT_REVISION\"\; >> "$2/version.c"
