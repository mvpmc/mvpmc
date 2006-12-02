#!/bin/sh
#
# Create the mvpmc build version information, unless we can prove that nothing
# has changed since the last build.
#

WORKING_DIR=$1
DIR=$2
VERSION="0.3.2-pre"

if [ "$WORKING_DIR" = "" -o "$DIR" = "" ] ; then
    exit 1
fi

GIT_REV_LIST=`which git-rev-list`
if [ "$GIT_REV_LIST" != "" ] ; then
    GIT_REVISION=`git-rev-list --all | head -1`
    GIT_DIFF_SIZE=`git-diff | wc -c`
    if [ "$GIT_DIFF_SIZE" != "0" ] ; then
	GIT_DIFFS=`git-diff | md5sum | cut -d' ' -f1`
    else
	GIT_DIFFS=
    fi
    if [ "x$GIT_REVISION" != "x" ]; then
	NCHANGES=`git diff $GIT_REVISION | grep "^[+-]" | grep -v "^--- " | grep -v "^+++ " | wc -l`
	if [ $NCHANGES -ne 0 ]; then
	    GIT_REVISION="$GIT_REVISION with $NCHANGES local changes"
	fi
    fi
else
    GIT_REVISION=
    GIT_DIFFS=
fi

cd $WORKING_DIR

echo -n > "$DIR/version_new.c"

echo char git_revision[] =  \"$GIT_REVISION\"\; >> "$DIR/version_new.c"
echo char git_diffs[] =  \"$GIT_DIFFS\"\; >> "$DIR/version_new.c"

#
# If we have no git revision info, then we have to assume a rebuild is needed.
#
if [ "$GIT_REVISION" != "" ] ; then
    if [ -a $DIR/version.c ] ; then
	OLD_MD5=`grep git_ $DIR/version.c | md5sum | cut -d' ' -f1`
	NEW_MD5=`md5sum $DIR/version_new.c | cut -d' ' -f1`

	if [ "$OLD_MD5" = "$NEW_MD5" ] ; then
	    rm $DIR/version_new.c
	    exit 0
	fi
    fi
fi

mv $DIR/version_new.c $DIR/version.c

echo char compile_time[] = \"`LANG=C date`\" \; >> "$DIR/version.c"
echo char version_number[] =  \"$VERSION\"\; >> "$DIR/version.c"
echo char build_user[] =  \"$USER\"\; >> "$DIR/version.c"

