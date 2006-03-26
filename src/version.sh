#!/bin/sh

cd $1

echo char compile_time[] = \"`LANG=C date`\" \; > version.c
echo char version[] =  \"$VERSION\"\; >> version.c

