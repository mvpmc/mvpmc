/*
 *  Copyright (C) 2004-2006, John Honeycutt
 *  http://www.mvpmc.org/
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 */

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <expat.h>
#include "rtv.h"
#include "rtvlib.h"
#include "httpclient.h"
#include "deviceinfoclient.h"

#define MAX_TAG_SZ (100)
#define MAX_VAL_SZ (200)

typedef enum {
   RTV_TYPE  = (1<<0),
   RTV_NAME  = (1<<1),
   RTV_MDESC = (1<<2),
   RTV_MNAME = (1<<3),
   RTV_MNUM  = (1<<4),
   RTV_VER   = (1<<5),
   RTV_SNUM  = (1<<6),
   RTV_UDN   = (1<<7),
   //
   // Leave out RTV_VER as this is not reported by all models
   //
   RTV_EXPECTED_FIELDS = (RTV_TYPE | RTV_NAME | RTV_MDESC | RTV_MNAME | RTV_MNUM | RTV_SNUM | RTV_UDN)
} xml_fields_t;

static char       curr_tag[MAX_TAG_SZ+1] = "";
static char       val_buff[MAX_VAL_SZ+1] = "";
static XML_Parser parser; 
//static int        Depth;

static void XMLCALL
devinfo_xml_start(void *data, const char *el, const char **attr) {
//   int i;
   data = data; attr = attr; //shut compiler up
   
   snprintf(curr_tag, MAX_TAG_SZ, "%s", el);

//   printf("XML_PARSE: (%d): elsz=%d\n", Depth, strlen(el)); 
//   for (i = 0; i < Depth; i++)
//      printf("  ");
   
//   printf("%s", el);
//   for (i = 0; attr[i]; i += 2) {
//     printf(" %s='%s'", attr[i], attr[i + 1]);
//   } 
//   printf("\n");
//   Depth++;
}  /* End of start handler */

static void XMLCALL
devinfo_xml_end(void *data, const char *el) {
   rtv_device_info_t *devinfo = data;

   el = el; //shut compiler up

//   printf("XML: EndHandler: %s=%s\n", curr_tag, val_buff);
   if ( strcmp(curr_tag, "deviceType") == 0 ) {
      devinfo->deviceType = malloc(strlen(val_buff) + 1);
      strcpy(devinfo->deviceType, val_buff);
      devinfo->status |= RTV_TYPE;
   }
   else if ( strcmp(curr_tag, "friendlyName") == 0 ) {
      devinfo->name = malloc(strlen(val_buff) + 1);
      strcpy(devinfo->name, val_buff);
      devinfo->status |= RTV_NAME;
   }
   else if ( strcmp(curr_tag, "modelDescription") == 0 ) {
      devinfo->modelDescr = malloc(strlen(val_buff) + 1);
      strcpy(devinfo->modelDescr, val_buff);
      devinfo->status |= RTV_MDESC;
   }
   else if ( strcmp(curr_tag, "modelName") == 0 ) {
      devinfo->modelName = malloc(strlen(val_buff) + 1);
      strcpy(devinfo->modelName, val_buff);
      devinfo->status |= RTV_MNAME;
   }
   else if ( strcmp(curr_tag, "modelNumber") == 0 ) {
      devinfo->modelNumber = malloc(strlen(val_buff) + 1);
      strcpy(devinfo->modelNumber, val_buff);
      devinfo->status |= RTV_MNUM;
   }
   else if ( strcmp(curr_tag, "version") == 0 ) {
      devinfo->versionStr = malloc(strlen(val_buff) + 1);
      strcpy(devinfo->versionStr, val_buff);
      devinfo->status |= RTV_VER;
   }
   else if ( strcmp(curr_tag, "serialNumber") == 0 ) {
      devinfo->serialNum = malloc(strlen(val_buff) + 1);
      strcpy(devinfo->serialNum, val_buff);
      devinfo->status |= RTV_SNUM;
   }
   else if ( strcmp(curr_tag, "UDN") == 0 ) {
      devinfo->udn = malloc(strlen(val_buff) + 1);
      strcpy(devinfo->udn, val_buff);
      devinfo->status |= RTV_UDN;
   }
 
   curr_tag[0] = '\0';
//   Depth--;
}  /* End of end handler */

static void XMLCALL devinfo_xml_charhndlr(void *userData, const XML_Char *s, int len)
{
   const char *strp = s;

   userData = userData; //shut compiler up

   // If all whitespace just discard
   while ( len ) {
      if ( (strp[0] != 0x9) && (strp[0] != 0xa) && (strp[0] != 0x20) ) {
         break;
      }
      strp++;len--;
   }
   if ( len == 0) {
      return;
   }

   if ( len >= MAX_VAL_SZ ) {
      len = MAX_VAL_SZ - 1;
   }
   snprintf(val_buff, len+1, "%s", strp);
//   printf("XML: CHAR: len=%d (%s)\n", len, val_buff);
}

static void XMLCALL
devinfo_default_handler(void *userData, const XML_Char *s, int len)
{
   userData=userData; s=s; len=len; //shut compiler up
}


static int get_deviceinfo_callback(unsigned char *buf, size_t len,  void *info)
{
   int done =1;
   
   info=info; //shut compiler up

   if (XML_Parse(parser, buf, len, done) == XML_STATUS_ERROR) {
      RTV_ERRLOG("%s at line %d\n",
                 XML_ErrorString(XML_GetErrorCode(parser)),
                 XML_GetCurrentLineNumber(parser));
      return(0);
   }
   XML_ParserFree(parser);
   return(0);
}

static int parse_version_info( rtv_device_info_t *devinfo )
{
   if ( strncmp(devinfo->modelNumber, "4999", 4) == 0 ) {
      // Dvarchive.
      //
      if ( rtv_globals.rtv_emulate_mode == RTV_DEVICE_4K ) {
         devinfo->version.vintage = RTV_DEVICE_4K;
         devinfo->version.major   = 4;
         devinfo->version.minor   = 3;
         devinfo->version.build   = 999; //use build 999 to designate Dvarchive
      }
      else {
         devinfo->version.vintage = RTV_DEVICE_5K;
         devinfo->version.major   = 5;
         devinfo->version.minor   = 1;
         devinfo->version.build   = 999; //use build 999 to designate Dvarchive
      }
      return(0);
   }

   if ( (strncmp(devinfo->modelNumber, "4", 1) == 0) ) {
      // 4K's don't seem to report a version string
      //
      devinfo->version.vintage = RTV_DEVICE_4K;
      devinfo->version.major   = 4;
      devinfo->version.minor   = 3;
      devinfo->version.build   = 111;
   }
   else if ( (strncmp(devinfo->versionStr, "530", 3) == 0) ) {
      devinfo->version.vintage = RTV_DEVICE_5K;
      devinfo->version.major   = devinfo->versionStr[3] - '0';
      devinfo->version.minor   = devinfo->versionStr[4] - '0';
      devinfo->version.build   = atoi(devinfo->versionStr+5);
   }
   else {
      RTV_ERRLOG("Unsupported RTV SW Version\n");
      rtv_print_device_info(devinfo);
      fflush(NULL);
      return(-1);
   }
   
   return(0);
}

//+***********************************************************************************
//                         PUBLIC FUNCTIONS
//+***********************************************************************************

//
// rtv_get_device_info
// returns pointer to replaytv device.
//
int rtv_get_device_info(const char *address, char *queryStr, rtv_device_t **device_p)
{
   char               url[512];
   struct hc         *hc;
   int                rc, new_entry;
   rtv_device_t      *rtv;
   rtv_device_info_t *devinfo;

   RTV_DBGLOG(RTVLOG_DSCVR, "%s: Enter: address=%s: current_rtv_cnt=%d\n", __FUNCTION__, address, rtv_devices.num_rtvs);

   *device_p  = NULL;
   rtv     = rtv_get_device_struct(address, &new_entry);
   devinfo = &(rtv->device);

   if ( !(new_entry) ) {
      RTV_DBGLOG(RTVLOG_DSCVR, "%s: address=%s already exists in device struct. Decrement count.\n", __FUNCTION__, address);
      rtv_devices.num_rtvs--;
   } 

   rtv_free_device_info(devinfo);
   devinfo->ipaddr = malloc(strlen(address) + 1);
   strcpy(devinfo->ipaddr, address);

   parser = XML_ParserCreate("US-ASCII");
   XML_SetElementHandler(parser, devinfo_xml_start, devinfo_xml_end);
   XML_SetCharacterDataHandler(parser, devinfo_xml_charhndlr);
   XML_SetDefaultHandler(parser, devinfo_default_handler);
   XML_SetUserData(parser, devinfo);
   
   if ( queryStr == NULL ) {
      sprintf(url, "http://%s/Device_Descr.xml", address);
      RTV_DBGLOG(RTVLOG_DSCVR, "%s: Build default query str: %s\n", __FUNCTION__, url);
   }
   else {
      strncpy(url, queryStr, 511);
      RTV_DBGLOG(RTVLOG_DSCVR, "%s: Use supplied query str: %s\n", __FUNCTION__, url);
   }
   
   hc = hc_start_request(url);
   if (!hc) {
      RTV_ERRLOG("%s: hc_start_request(): %d=>%s\n", __FUNCTION__, errno, strerror(errno));
      rtv_free_device_info(devinfo);
      return(-EPROTO);
   }
   
   hc_send_request(hc, NULL);
   rc = hc_read_pieces(hc, get_deviceinfo_callback, NULL, 0);
   hc_free(hc);
   if ( rc != 0 ) {
      RTV_ERRLOG("%s: hc_read_pieces call failed: rc=%d\n", __FUNCTION__, rc);
      rtv_free_device_info(devinfo);
      return(rc);
   }

   if ( (devinfo->status & RTV_EXPECTED_FIELDS) != RTV_EXPECTED_FIELDS ) {
      RTV_ERRLOG("%s: Missing XML Fields exp=%08X got=%08lX\n", __FUNCTION__, RTV_EXPECTED_FIELDS, devinfo->status);
      rtv_free_device_info(devinfo);
      return(-EBADE);
   }

   if ( (rc = parse_version_info(devinfo)) != 0 ) {
      
   }

   *device_p = rtv;
   rtv_devices.num_rtvs++;
   if ( RTVLOG_DSCVR ) {
      rtv_print_device_info(devinfo);
   }
   return (rc);
}

void rtv_free_device_info( rtv_device_info_t *devinfo ) 
{
   if ( devinfo != NULL ) {
      if ( devinfo->ipaddr      != NULL ) free(devinfo->ipaddr);
      if ( devinfo->deviceType  != NULL ) free(devinfo->deviceType);
      if ( devinfo->name        != NULL ) free(devinfo->name);
      if ( devinfo->modelDescr  != NULL ) free(devinfo->modelDescr);
      if ( devinfo->modelName   != NULL ) free(devinfo->modelName);
      if ( devinfo->modelNumber != NULL ) free(devinfo->modelNumber);
      if ( devinfo->versionStr  != NULL ) free(devinfo->versionStr);
      if ( devinfo->serialNum   != NULL ) free(devinfo->serialNum);
      if ( devinfo->udn         != NULL ) free(devinfo->udn);
   }
   memset(devinfo, 0, sizeof(rtv_device_info_t));
}

void rtv_print_device_info( const rtv_device_info_t *devinfo ) 
{
   if ( devinfo != NULL ) {
      RTV_PRT("RTV Device info\n");
      RTV_PRT("---------------\n");
      RTV_PRT("  ipaddr:      %s\n", devinfo->ipaddr);
      RTV_PRT("  deviceType:  %s\n", devinfo->deviceType);
      RTV_PRT("  name:        %s\n", devinfo->name);
      RTV_PRT("  modelDescr:  %s\n", devinfo->modelDescr);
      RTV_PRT("  modelName:   %s\n", devinfo->modelName);
      RTV_PRT("  modelNumber: %s\n", devinfo->modelNumber);
      RTV_PRT("  versionStr:  %s\n", devinfo->versionStr);
      RTV_PRT("     vintage:  %d\n", devinfo->version.vintage);
      RTV_PRT("     major:    %d\n", devinfo->version.major);
      RTV_PRT("     minor:    %d\n", devinfo->version.minor);
      RTV_PRT("     build:    %d\n", devinfo->version.build);
      RTV_PRT("  serialNum:   %s\n", devinfo->serialNum);
      RTV_PRT("  udn:         %s\n", devinfo->udn);
      RTV_PRT("  vintage:     %d\n", devinfo->version.vintage);
      RTV_PRT("  status:      %08lX\n", devinfo->status);
   }
   else {
      RTV_PRT("RTV Device info object is NULL!\n");
   }
}
