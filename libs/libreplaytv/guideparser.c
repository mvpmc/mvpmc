/*
 *  Copyright (C) 2004, John Honeycutt
 *  http://mvpmc.sourceforge.net/
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

/*******************************************************************************************************
 * NOTE: This code is based on the ReplayTV 5000 Guide Parser
 *  by Lee Thompson <thompsonl@logh.net> Dan Frumin <rtv@frumin.com>, Todd Larason <jtl@molehill.org>
 *******************************************************************************************************/

#if defined(__unix__) && !defined(__FreeBSD__)
#   include <netinet/in.h>
#endif
#include <string.h>
#include <memory.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "rtv.h"
#include "rtvlib.h"
#include "guideparser.h"


//+***********************************************************************************************
//  Data structure endian conversion functions
//+***********************************************************************************************

//-------------------------------------------------------------------------
static void convertThemeInfoEndian(theme_info_t *strThemeInfo)
{
   strThemeInfo->flags = ntohl(strThemeInfo->flags);
   strThemeInfo->suzuki_id = ntohl(strThemeInfo->suzuki_id);
   strThemeInfo->thememinutes = ntohl(strThemeInfo->thememinutes);
   return;
}

//-------------------------------------------------------------------------
static void convertChannelInfoEndian(channel_info_t *strChannelInfo)
{
   strChannelInfo->channel = ntohs(strChannelInfo->channel);
   strChannelInfo->channelindex = ntohl(strChannelInfo->channelindex);
   strChannelInfo->isvalid = ntohl(strChannelInfo->isvalid);
   strChannelInfo->structuresize = ntohl(strChannelInfo->structuresize);
   strChannelInfo->tmsID = ntohl(strChannelInfo->tmsID);
   strChannelInfo->usetuner = ntohl(strChannelInfo->usetuner);   
   return;
}

//-------------------------------------------------------------------------
static void convertProgramInfoEndian(program_info_t *strProgramInfo)
{
   strProgramInfo->autorecord = ntohl(strProgramInfo->autorecord);
   strProgramInfo->eventtime = ntohl(strProgramInfo->eventtime);
   strProgramInfo->flags = ntohl(strProgramInfo->flags);
   strProgramInfo->isvalid = ntohl(strProgramInfo->isvalid);
   strProgramInfo->minutes = ntohs(strProgramInfo->minutes);
   strProgramInfo->recLen = ntohs(strProgramInfo->recLen);
   strProgramInfo->structuresize = ntohl(strProgramInfo->structuresize);
   strProgramInfo->tuning = ntohl(strProgramInfo->tuning);
   strProgramInfo->tmsID = ntohl(strProgramInfo->tmsID);
   return;
}

//-------------------------------------------------------------------------
static void convertMovieInfoEndian(movie_info_t *strMovieInfo)
{
   strMovieInfo->mpaa = ntohs(strMovieInfo->mpaa);
   strMovieInfo->runtime = ntohs(strMovieInfo->runtime);
   strMovieInfo->stars = ntohs(strMovieInfo->stars);
   strMovieInfo->year = ntohs(strMovieInfo->year);
   return;
}

//-------------------------------------------------------------------------
static void convertPartsInfoEndian(parts_info_t *strPartsInfo)
{
   strPartsInfo->maxparts = ntohs(strPartsInfo->maxparts);
   strPartsInfo->partnumber = ntohs(strPartsInfo->partnumber);
   return;
}
//-------------------------------------------------------------------------
static void convertReplayShowEndian(replay_show_t *strReplayShow)
{
   strReplayShow->checkpointed = ntohl(strReplayShow->checkpointed);
   strReplayShow->created = ntohl(strReplayShow->created);
   strReplayShow->downloadid = ntohl(strReplayShow->downloadid);
   strReplayShow->GOP_count = ntohl(strReplayShow->GOP_count);
   strReplayShow->GOP_highest = ntohl(strReplayShow->GOP_highest);
   strReplayShow->GOP_last = ntohl(strReplayShow->GOP_last);
   strReplayShow->guaranteed = ntohl(strReplayShow->guaranteed);
   strReplayShow->guideid = ntohl(strReplayShow->guideid);
   strReplayShow->indexsize = ntohll(strReplayShow->indexsize);
   strReplayShow->inputsource = ntohl(strReplayShow->inputsource);
   strReplayShow->instance = ntohl(strReplayShow->instance);
   strReplayShow->intact = ntohl(strReplayShow->intact);
   strReplayShow->ivsstatus = ntohl(strReplayShow->ivsstatus);
   strReplayShow->mpegsize = ntohll(strReplayShow->mpegsize);
   strReplayShow->playbackflags = ntohl(strReplayShow->playbackflags);
   strReplayShow->quality = ntohl(strReplayShow->quality);
   strReplayShow->recorded = ntohl(strReplayShow->recorded);
   strReplayShow->seconds = ntohl(strReplayShow->seconds);
   strReplayShow->timessent = ntohl(strReplayShow->timessent);
   strReplayShow->upgradeflag = ntohl(strReplayShow->upgradeflag);
   strReplayShow->unused = ntohs(strReplayShow->unused);
   return;
}

//-------------------------------------------------------------------------
static void convertReplayChannelEndian(replay_channel_t *strReplayChannel)
{
   strReplayChannel->created = ntohl(strReplayChannel->created);
   strReplayChannel->timereserved = ntohll(strReplayChannel->timereserved);
   strReplayChannel->allocatedspace = ntohll(strReplayChannel->allocatedspace);
   strReplayChannel->category = ntohl(strReplayChannel->category);
   strReplayChannel->channeltype = ntohl(strReplayChannel->channeltype);
   strReplayChannel->keep = ntohl(strReplayChannel->keep);
   strReplayChannel->stored = ntohl(strReplayChannel->stored);
   strReplayChannel->quality = ntohl(strReplayChannel->quality);
   
   {
      strReplayChannel->unknown1 = ntohl(strReplayChannel->unknown1);
      strReplayChannel->unknown2 = ntohl(strReplayChannel->unknown2);
      strReplayChannel->unknown3 = ntohl(strReplayChannel->unknown3);
      strReplayChannel->unknown4 = ntohl(strReplayChannel->unknown4);
      strReplayChannel->unknown5 = ntohl(strReplayChannel->unknown5);
      strReplayChannel->unknown6 = ntohl(strReplayChannel->unknown6);
      strReplayChannel->unknown7 = ntohl(strReplayChannel->unknown7);
      strReplayChannel->unknown8 = ntohl(strReplayChannel->unknown8);
      strReplayChannel->unknown9 = ntohl(strReplayChannel->unknown9);
      strReplayChannel->unknown10 = ntohl(strReplayChannel->unknown10);
      strReplayChannel->unknown11 = ntohl(strReplayChannel->unknown11);
      strReplayChannel->unknown12 = ntohl(strReplayChannel->unknown12);
   }   
   return;
}

//-------------------------------------------------------------------------
static void convertReplayGuideEndian(guide_header_t *strGuideHeader)
{
   unsigned int cc;

   strGuideHeader->groupData.categories = ntohl(strGuideHeader->groupData.categories);
   strGuideHeader->groupData.structuresize = ntohl(strGuideHeader->groupData.structuresize);
   
   strGuideHeader->guideSnapshotHeader.snapshotsize = ntohl(strGuideHeader->guideSnapshotHeader.snapshotsize);
   strGuideHeader->guideSnapshotHeader.channeloffset = ntohl(strGuideHeader->guideSnapshotHeader.channeloffset);
   strGuideHeader->guideSnapshotHeader.flags = ntohl(strGuideHeader->guideSnapshotHeader.flags);
   strGuideHeader->guideSnapshotHeader.groupdataoffset = ntohl(strGuideHeader->guideSnapshotHeader.groupdataoffset);
   strGuideHeader->guideSnapshotHeader.osversion = ntohs(strGuideHeader->guideSnapshotHeader.osversion);
   strGuideHeader->guideSnapshotHeader.snapshotversion = ntohs(strGuideHeader->guideSnapshotHeader.snapshotversion);
   strGuideHeader->guideSnapshotHeader.channelcount = ntohl(strGuideHeader->guideSnapshotHeader.channelcount);
   strGuideHeader->guideSnapshotHeader.channelcountcheck = ntohl(strGuideHeader->guideSnapshotHeader.channelcountcheck);
   strGuideHeader->guideSnapshotHeader.showoffset = ntohl(strGuideHeader->guideSnapshotHeader.showoffset);
   strGuideHeader->guideSnapshotHeader.bytesfree = ntohll(strGuideHeader->guideSnapshotHeader.bytesfree);
   strGuideHeader->guideSnapshotHeader.structuresize = ntohl(strGuideHeader->guideSnapshotHeader.structuresize);
   
   strGuideHeader->guideSnapshotHeader.unknown1 = ntohl(strGuideHeader->guideSnapshotHeader.unknown1);
   strGuideHeader->guideSnapshotHeader.unknown2 = ntohl(strGuideHeader->guideSnapshotHeader.unknown2);
   strGuideHeader->guideSnapshotHeader.unknown3 = ntohl(strGuideHeader->guideSnapshotHeader.unknown3);
   strGuideHeader->guideSnapshotHeader.unknown6 = ntohl(strGuideHeader->guideSnapshotHeader.unknown6);
   strGuideHeader->guideSnapshotHeader.unknown7 = ntohl(strGuideHeader->guideSnapshotHeader.unknown7);
   
   for( cc = 0; cc < strGuideHeader->groupData.categories; ++cc )
   {
      strGuideHeader->groupData.category[cc] = ntohl(strGuideHeader->groupData.category[cc]);
      strGuideHeader->groupData.categoryoffset[cc] = ntohl(strGuideHeader->groupData.categoryoffset[cc]);
   }   
   return;
}


//+***********************************************************************************************
//  Text formatting functions
//+***********************************************************************************************

static char* convertCodepage(char *szString)
{
    unsigned int i = 0;
    char         ch;
    
    if (szString[0] == 0) {
        return(szString);;
    }
    
    for( i = 0; i < strlen(szString); ++i ) {
        ch = szString[i];   
        if (ch == (char)146) {
            szString[i] = '\'';
        }
        if (ch == (char)147) {
            szString[i] = '\"';
        }
        if (ch == (char)148) {
            szString[i] = '\"';
        }
    }
    return(szString);    
}

static int parse_show(replay_show_t *show_rec, rtv_show_export_t *sh)
{
   char *bufptr;

   convertReplayShowEndian(show_rec);
   convertProgramInfoEndian(&(show_rec->programInfo));
   convertChannelInfoEndian(&(show_rec->channelInfo));
   
   bufptr = show_rec->programInfo.szDescription;
   
   if ( RTVLOG_GUIDE ) {
      hex_dump("SHOW_DUMP", (char*)show_rec, 512);
   }

   // process the record's flags
   //
   if (show_rec->guaranteed == 0xFFFFFFFF) {
      sh->flags.guaranteed= TRUE;
   }     
   if (show_rec->guideid != 0) {
      sh->flags.guide_id = TRUE;
   }
   if (show_rec->programInfo.flags & 0x00000001) {
      sh->flags.cc = TRUE;
   }
   if (show_rec->programInfo.flags & 0x00000002) {
      sh->flags.stereo = TRUE;
   }
   if (show_rec->programInfo.flags & 0x00000004) {
      sh->flags.repeat = TRUE;
   }
   if (show_rec->programInfo.flags & 0x00000008) {
      sh->flags.sap = TRUE;
   }
   if (show_rec->programInfo.flags & 0x00000010) {
      sh->flags.letterbox = TRUE;
   }
   if (show_rec->programInfo.flags & 0x00000020) {
      sh->flags.movie = TRUE;
   }
   if (show_rec->programInfo.flags & 0x00000040) {
      sh->flags.multipart = TRUE;
   }
   
   if ( sh->flags.movie ) {  
      // Movie: first 8 bytes of description buff are movie info
      //
      movie_info_t *movie = (movie_info_t*)bufptr;
      bufptr += sizeof(movie_info_t);
      convertMovieInfoEndian(movie);
      
      // JBH: todo: need to parse the movie info
      //
      RTV_DBGLOG(RTVLOG_GUIDE, "Parse movie_info_t:\n");
      RTV_DBGLOG(RTVLOG_GUIDE, "    mpaa: %d\n", movie->mpaa);
      RTV_DBGLOG(RTVLOG_GUIDE, "    stars: %d\n", movie->stars);
      RTV_DBGLOG(RTVLOG_GUIDE, "    year: %d\n", movie->year);
      RTV_DBGLOG(RTVLOG_GUIDE, "    runtime: %d\n", movie->runtime);
   }
   
   if (sh->flags.multipart) {
      // Multiple Parts
      //
      parts_info_t *parts = (parts_info_t*)bufptr;
      bufptr += sizeof(parts_info_t);
      convertPartsInfoEndian(parts);

      // JBH: todo: need to parse the parts info
      //
      RTV_DBGLOG(RTVLOG_GUIDE, "Parse parts_info_t:\n");
      RTV_DBGLOG(RTVLOG_GUIDE, "    partnumber: %d\n", parts->partnumber);
      RTV_DBGLOG(RTVLOG_GUIDE, "    maxparts:   %d\n", parts->maxparts);
   }
   
   // Process the record's description buffer
   //
   sh->title = malloc(show_rec->programInfo.titleLen);
   strncpy(sh->title, convertCodepage(bufptr), show_rec->programInfo.titleLen);
   bufptr += show_rec->programInfo.titleLen;

   sh->episode = malloc(show_rec->programInfo.episodeLen);
   strncpy(sh->episode, convertCodepage(bufptr), show_rec->programInfo.episodeLen);
   bufptr += show_rec->programInfo.episodeLen;
   
   sh->description = malloc(show_rec->programInfo.descriptionLen);
   strncpy(sh->description, convertCodepage(bufptr), show_rec->programInfo.descriptionLen);
   bufptr += show_rec->programInfo.descriptionLen;
    
   sh->actors = malloc(show_rec->programInfo.actorLen);
   strncpy(sh->actors, convertCodepage(bufptr), show_rec->programInfo.actorLen);
   bufptr += show_rec->programInfo.actorLen;
   
   sh->guest = malloc(show_rec->programInfo.guestLen);
   strncpy(sh->guest, convertCodepage(bufptr), show_rec->programInfo.guestLen);
   bufptr += show_rec->programInfo.guestLen;
  
   sh->suzuki = malloc(show_rec->programInfo.suzukiLen);
   strncpy(sh->suzuki, convertCodepage(bufptr), show_rec->programInfo.suzukiLen);
   bufptr += show_rec->programInfo.suzukiLen;
   
   sh->producer = malloc(show_rec->programInfo.producerLen);
   strncpy(sh->producer, convertCodepage(bufptr), show_rec->programInfo.producerLen);
   bufptr += show_rec->programInfo.producerLen;
   
   sh->director = malloc(show_rec->programInfo.directorLen);
   strncpy(sh->director, convertCodepage(bufptr), show_rec->programInfo.directorLen);
   bufptr += show_rec->programInfo.directorLen;

   // mpg filename
   //
   sh->file_name = malloc(20);
   snprintf(sh->file_name, 19, "%lu.mpg", show_rec->recorded);
   
   return(0);
}


static void print_v2_guide_snapshot_header(v2_guide_snapshot_header_t  *rtv2hdr ) 
{
   if ( rtv2hdr != NULL ) {
      RTV_PRT("Guide Snapshot Header:\n");
      RTV_PRT("  osversion:             0x%04x (%u)\n", rtv2hdr->osversion, rtv2hdr->osversion);
      RTV_PRT("  snapshotversion:       0x%04x (%u)\n", rtv2hdr->snapshotversion, rtv2hdr->snapshotversion);
      RTV_PRT("  structuresize:     0x%08lx (%lu)\n", rtv2hdr->structuresize, rtv2hdr->structuresize);
      RTV_PRT("  channelcount:      0x%08lx (%lu)\n", rtv2hdr->channelcount, rtv2hdr->channelcount);
      RTV_PRT("  channelcountcheck: 0x%08lx (%lu)\n", rtv2hdr->channelcountcheck, rtv2hdr->channelcountcheck);
      RTV_PRT("  groupdataoffset:   0x%08lx (%lu)\n", rtv2hdr->groupdataoffset, rtv2hdr->groupdataoffset);
      RTV_PRT("  channeloffset:     0x%08lx (%lu)\n", rtv2hdr->channeloffset, rtv2hdr->channeloffset);
      RTV_PRT("  showoffset:        0x%08lx (%lu)\n", rtv2hdr->showoffset, rtv2hdr->showoffset);
      RTV_PRT("  snapshotsize:      0x%08lx (%lu)\n", rtv2hdr->snapshotsize, rtv2hdr->snapshotsize);
      //RTV_PRT("  bytesfree:         0x%016llx\n", rtv2hdr->bytesfree); //Doesn't seem to really be bytes free
      RTV_PRT("  flags:             0x%08lx\n", rtv2hdr->flags);
   }
   else {
      RTV_PRT("Guide Header object is NULL!\n");
   }
}

static void print_groupdata_header(group_data_t  *gdatahdr ) 
{
   unsigned int x;

   if (gdatahdr  != NULL ) {
      RTV_PRT("Group Data Header:\n");
      RTV_PRT("  structuresize:     0x%08lx (%lu)\n", gdatahdr->structuresize, gdatahdr->structuresize);
      RTV_PRT("  num_categories:    0x%08lx (%lu)\n", gdatahdr->categories, gdatahdr->categories);

      for( x = 0; x < gdatahdr->categories; x++ ) {
         RTV_PRT("    %u:  cat_idx=0x%08lX name_offset=0x%08lX\n", x, gdatahdr->category[x], gdatahdr->categoryoffset[x]);
      }

   }
   else {
      RTV_PRT("Guide Header object is NULL!\n");
   }
}

static void print_category_array( category_array_t  ca[], unsigned int entries) 
{
   unsigned int x;
   
   RTV_PRT("Category Array: num_entries=%u\n", entries);
   for( x = 0; x < entries; x++ ) {
      RTV_PRT("    %u:  cat_idx=0x%08lX name=%s\n", x, ca[x].index, ca[x].szName);
   }
}




//+***********************************************************************************************
//  Name: rtv_struct_chk
//+***********************************************************************************************
static int rtv_struct_chk(void) 
{
   int rc = 0;
   if (sizeof(program_info_t) != PROGRAMINFO_SZ) {
      RTV_ERRLOG("program_info_t structure, needs to be %u instead of %u\n",PROGRAMINFO_SZ,sizeof(program_info_t));
      rc = -1;
   }
   if (sizeof(channel_info_t) != CHANNELINFO_SZ) {
      RTV_ERRLOG("channel_info_t structure, needs to be %u instead of %u\n",CHANNELINFO_SZ,sizeof(channel_info_t));
      rc = -1;
   }
   if (sizeof(replay_show_t) != REPLAYSHOW_SZ) {
      RTV_ERRLOG("replay_show_t structure, needs to be %u instead of %u\n",REPLAYSHOW_SZ,sizeof(replay_show_t));
      rc = -1;
   }
   if (sizeof(replay_channel_t) != REPLAYCHANNEL_SZ) {
      RTV_ERRLOG("replay_channel_t structure, needs to be %u instead of %u\n",REPLAYCHANNEL_SZ,sizeof(replay_channel_t));
      rc = -1;
   }
   if (sizeof(guide_header_t) != GUIDEHEADER_SZ) {
      RTV_ERRLOG("guide_header_t structure, needs to be %u instead of %u\n",GUIDEHEADER_SZ,sizeof(guide_header_t));
      rc = -1;
   }
   return(rc);
}


//+***********************************************************************************
//                         PUBLIC FUNCTIONS
//+***********************************************************************************

int parse_v2_guide_snapshot(const char *guideDump, rtv_guide_export_t *guideExport)
{
   guide_header_t             *guidehdr       = (guide_header_t*)guideDump;
   v2_guide_snapshot_header_t *sshdr          = &(guidehdr->guideSnapshotHeader);
   group_data_t               *gdatahdr       = &(guidehdr->groupData);
   rtv_show_export_t         **rtvShowExportP = &(guideExport->rec_show_list);

   rtv_show_export_t          *rtvShowExport;
   replay_show_t              *show_rec;
   category_array_t            catArray[GRP_DATA_NUM_CATEGORIES];
   unsigned int                x;
   unsigned int                memAllocShows, numShows;
   int                         rc;

   memset(catArray, 0, sizeof(catArray));
   
   if ( rtv_struct_chk() != 0 ) {
      return(-1);
   }

   RTV_DBGLOG(RTVLOG_GUIDE, "guide struct base address: %p\n", guideDump); 
   convertReplayGuideEndian((guide_header_t*)guideDump);

   // guide header error checking & some setup.
   //
   if ( (sshdr->osversion != 0) || (sshdr->snapshotversion != 2) ) { 
      RTV_ERRLOG("Guide file is either damaged or from an older ReplayTV\n");
      RTV_ERRLOG("    osver.snapshotver=%d.%d   shected=0.2\n", sshdr->osversion, sshdr->snapshotversion);
      print_v2_guide_snapshot_header(sshdr);
      return(-1);
   }

   if (sshdr->channelcount != sshdr->channelcountcheck) {
      RTV_ERRLOG("channelcount mismatch: Guide Snapshot might be damaged\n");
      print_v2_guide_snapshot_header(sshdr);
      return(-1);
   }

   memAllocShows = sshdr->snapshotsize - sshdr->showoffset;
   if ( (memAllocShows % sizeof(replay_show_t)) != 0 ) { 
      RTV_ERRLOG("Invalid show storage size: allocated=%u show_sz=%u\n", memAllocShows, sizeof(replay_show_t));
      print_v2_guide_snapshot_header(sshdr);
      return(-1);
   }
   numShows = memAllocShows / sizeof(replay_show_t);

   // Copy categories into a buffer
   //
   for( x = 0; x < gdatahdr->categories; x++ ) {
      strncpy(catArray[x].szName, gdatahdr->catbuffer + gdatahdr->categoryoffset[x],sizeof(catArray[x].szName)-1);
      catArray[x].index = 1 << gdatahdr->category[x];
   }
    
   if ( RTVLOG_GUIDE ) {
      print_v2_guide_snapshot_header(sshdr);
      print_groupdata_header(gdatahdr);
      print_category_array(catArray, gdatahdr->categories);
   }

   // Process the recorded shows
   //
   show_rec        = (replay_show_t*)(guideDump + sshdr->showoffset);
   *rtvShowExportP = malloc(sizeof(rtv_show_export_t) * numShows);
   rtvShowExport   = *rtvShowExportP;

   memset(rtvShowExport, 0, sizeof(rtv_show_export_t) * numShows);

   RTV_DBGLOG(RTVLOG_GUIDE, "show array base address:   %p    num_shows=%d\n", show_rec, numShows); 
   for( x = 0 ; x < numShows; x++ ) {
      RTV_DBGLOG(RTVLOG_GUIDE, "show[%u] struct base address: %p\n", x, &(show_rec[x])); 
      if ( (rc = parse_show( &(show_rec[x]), &(rtvShowExport[x]))) != 0 ) {
         return(rc);
      }

      if ( RTVLOG_GUIDE ) {
         rtv_print_show(&(rtvShowExport[x]), x);
      }
   }

   guideExport->num_rec_shows = numShows;
   return(0);
}
