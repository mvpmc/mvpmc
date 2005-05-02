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


#define V1_CHAN_REC_SIZE (624)
#define DAYSOFWEEK_STR_SZ (32)
#define SHOW_RATING_STR_SZ (40)
#define GENRE_STR_SZ (70)

typedef enum rtv_guide_ver_t 
{
   RTV_GV_UNKNOWN = 0,
   RTV_GV_1       = 1,
   RTV_GV_2       = 2,
} rtv_guide_ver_t;

//+***********************************************************************************************
//  String conversion functions
//+***********************************************************************************************

typedef struct str_xref_t
{
   int   key;
   char *val;
} str_xref_t;

static char* xref_string(int key, str_xref_t *data_st)
{
   int idx = 0;
   while ( data_st[idx].key != -1 ) {
      if ( data_st[idx].key == key ) {
         break;
      }
      idx++;
   }
   return(data_st[idx].val);
}

static char* xref_as_mask_string(int key, str_xref_t *data_st)
{
   int idx = 0;
   while ( data_st[idx].key != -1 ) {
      if ( key & data_st[idx].key ) {
         break;
      }
      idx++;
   }
   return(data_st[idx].val);
}

// Recording Quality
//
str_xref_t rtv_quality[] =
{
   { RTV_QUALITY_HIGH, "High" }, 
   { RTV_QUALITY_MEDIUM, "Medium"} , 
   { RTV_QUALITY_STANDARD, "Standard" }, 
   { -1, "" }
};

char *rtv_xref_quality(int key)
{
   return(xref_string(key, rtv_quality));
}

// Input Source
//
str_xref_t input_source[] =
{
   { 0, "Direct RF" }, 
   { 1, "Direct Line 1"} , 
   { 2, "Direct Line 2" }, 
   { 3, "Tuner"} , 
   { 16, "Pre-loaded" }, 
   { -1, "" }
};
char *rtv_xref_input_source(int key)
{
   return(xref_string(key, input_source));
}


// Show Genre
//
str_xref_t show_genre[] =
{
   {1,"Action"},
   {2,"Adult"},
   {3,"Adventure"},
   {4,"Animals"},
   {5,"Animated"},
   {8,"Art"},
   {9,"Automotive"},
   {10,"AwardShow"},
   {12,"Baseball"},
   {13,"Baseketball"},
   {14,"Beauty"},
   {15,"Bicycling"},
   {16,"Billiards"},
   {17,"Biography"},
   {18,"Boating"},
   {19,"BodyBuilding"},
   {21,"Boxing"},
   {26,"Children"},
   {30,"ClassicTV"},
   {31,"Collectibles"},
   {32,"Comedy"},
   {33,"Comedy-Drama"},
   {34,"Computers"},
   {35,"Cooking"},
   {36,"Crime"},
   {37,"CrimeDrama"},
   {39,"Dance"},
   {43,"Drama"},
   {44,"Educational"},
   {45,"Electronics"},
   {47,"Exercise"},
   {48,"Family"},
   {49,"Fantasy"},
   {50,"Fashion"},
   {52,"Fishing"},
   {53,"Football"},
   {55,"Fundraising"},
   {56,"GameShow"},
   {57,"Golf"},
   {58,"Gymnastics"},
   {59,"Health"},
   {60,"History"},
   {61,"HistoricalDrama"},
   {62,"Hockey"},
   {68,"Holiday"},
   {69,"Horror"},
   {70,"Horses"},
   {71,"Home&Garden"},
   {72,"Housewares"},
   {73,"How-To"},
   {74,"International"},
   {75,"Interview"},
   {76,"Jewelry"},
   {77,"Lacross"},
   {78,"Magazine"},
   {79,"MartialArts"},
   {80,"Medical"},
   {83,"Motorcycles"},
   {90,"Mystery"},
   {91,"Nature"},
   {92,"News"},
   {94,"Olympics"},
   {96,"Outdoors"},
   {99,"PublicAffairs"},
   {100,"Racing"},
   {102,"RealityShow"},
   {103,"Religious"},
   {104,"Rodeo"},
   {105,"Romance"},
   {106,"RomanticComedy"},
   {107,"Rugby"},
   {108,"Running"},
   {109,"Satire"},
   {110,"Science"},
   {111,"ScienceFiction"},
   {113,"Shopping"},
   {114,"Sitcom"},
   {115,"Skating"},
   {116,"Skiing"},
   {117,"SlegDog"},
   {118,"SnowSports"},
   {119,"SoapOpera"},
   {123,"Soccer"},
   {125,"Spanish"},
   {131,"Suspense"},
   {132,"SuspenseComedy"},
   {133,"Swimming"},
   {134,"TalkShow"},
   {135,"Tennis"},
   {136,"Thriller"},
   {137,"TrackandField"},
   {138,"Travel"},
   {139,"Variety"},
   {141,"War"},
   {143,"Weather"},
   {144,"Western"},
   {146,"Wrestling"},
   { -1, "" }
};

// Input device
//
str_xref_t input_device[] =
{
   { 64, "Standard" },
   { 65, "A Lineup" },
   { 66, "B Lineup" },
   { 68, "Rebuild Lineup" },
   { 71, "Non-Addressable Converter" },
   { 72, "Hamlin" },
   { 73, "Jerrold Impulse" },
   { 74, "Jerrold" },
   { 76, "Digital Rebuild" },
   { 77, "Multiple Converters" },
   { 78, "Pioneer" },
   { 79, "Oak" },
   { 80, "Premium" },
   { 82, "Cable-ready-TV" },
   { 83, "Converter Switch" },
   { 84, "Tocom" },
   { 85, "A Lineup Cable-ready-TV" },
   { 86, "B Lineup Cable-ready-TV" },
   { 87, "Scientific-Atlanta" },
   { 88, "Digital" },
   { 90, "Zenith" },
   { -1, "" }
};


// Service Tier
//
str_xref_t service_tier[] =
{
   { 1, "Basic" }, 
   { 2, "Expanded Basi" } , 
   { 3, "Premium" }, 
   { 4, "PPV" } , 
   { 5, "DMX/Music" }, 
   { -1, "" }
};

// IVS Status
//
str_xref_t ivs_status[] =
{
   { 0, "Local" }, 
   { 1, "LAN" } , 
   { 2, "Internet Downloadable" }, 
   { 3, "Internet Download Failed" }, 
   { 4, "Internet Index Download Restart" } , 
   { 5, "Internet Index Downloading" }, 
   { 6, "Internet Index Download Complete" }, 
   { 7, "Internet MPEG Download Restart" } , 
   { 8, "Internet MPEG Downloading" }, 
   { 9, "Internet MPEG Download Complete" } , 
   { 10, "Internet File Not Found" }, 
   { -1, "" }
};

// TV Rating
//
str_xref_t tv_rating[] =
{
   { 0x00000800, "TV-14" }, 
   { 0x00001000, "TV-G" }, 
   { 0x00002000, "TV-MA"} , 
   { 0x00004000, "TV-PG" }, 
   { 0x00008000, "TV-Y" }, 
   { 0x00010000, "TV-Y7"} , 
   { -1, "" }
};


// MPAA Rating
//
str_xref_t mpaa_rating[] =
{
   { 0x00000002, "G"} , 
   { 0x00000004, "NC-17" }, 
   { 0x00000008, "NR" }, 
   { 0x00000010, "PG"} , 
   { 0x00000020, "PG-13" }, 
   { 0x00000040, "R" }, 
   { -1, "" }
};

str_xref_t recording_type[] =
{
   { 1, "Recurring" }, 
   { 2, "Theme"} , 
   { 3, "Single" }, 
   { 4, "Zone" }, 
   { -1, "" }
};

static void mapDaysOfWeek(int dayofweek, char **daysStr)
{
   char *szDisplayString = malloc(DAYSOFWEEK_STR_SZ);
   strcpy( szDisplayString, "");
   
   if ( dayofweek & CE_SUN ) {
      strcat(szDisplayString, "SU ");
   }
   if ( dayofweek & CE_MON ) {
      strcat(szDisplayString, "MO ");
   }
   if ( dayofweek & CE_TUE ) {
      strcat(szDisplayString, "TU ");
   }
   if ( dayofweek & CE_WED ) {
      strcat(szDisplayString, "WE ");
   }
   if ( dayofweek & CE_THU ) {
      strcat(szDisplayString, "TH ");
   }
   if ( dayofweek & CE_FRI ) {
      strcat(szDisplayString, "FR ");
   }
   if ( dayofweek & CE_SAT ) {
      strcat(szDisplayString, "SA");
   }
   if ( dayofweek == 127) {
      // Display something less ugly!
      memset(szDisplayString,0,sizeof(szDisplayString));
      strcpy(szDisplayString, "Everyday");
   }
   
   *daysStr = szDisplayString;
}

static void mapExtendedTVRating(int tvrating, char **ratingStr)
{
   char *szRating = malloc(SHOW_RATING_STR_SZ);
   strcpy( szRating, "");
   
   if (tvrating &  0x00020000) {
      strcat( szRating, "S" );    // Sex
   }
   if (tvrating &  0x00040000) {
      strcat( szRating, "V" );    // Violence
   }
   if (tvrating &  0x00080000) {
      strcat( szRating, "L" );    // Language
   }
   if (tvrating &  0x00100000) {
      strcat( szRating, "D" );    // Drug Use
   }
   if (tvrating &  0x00200000){
      strcat( szRating, "F" );    // Fantasy Violence
   }

   *ratingStr = szRating;
}

static void mapExtendedMPAARating(int mpaarating, char **ratingStr)
{
   char *szRating = malloc(SHOW_RATING_STR_SZ);
   strcpy( szRating, "");
   
   if (mpaarating & 0x00400000) {
      strcat( szRating, "AC " );    // Adult Content
   }
   if (mpaarating & 0x00800000) {
      strcat( szRating, "BN " );   // Brief Nudity
   }
   if (mpaarating & 0x01000000) {
      strcat( szRating, "GL " );   // Graphic Language
   }
   if (mpaarating & 0x02000000) {
      strcat( szRating, "GV " );   // Graphic Violence
   }
   if (mpaarating & 0x04000000) {
      strcat( szRating, "AL " );   // Adult Language
   }
   if (mpaarating & 0x08000000) {
      strcat( szRating, "MV " );   // Mild Violence
   }
   if (mpaarating & 0x10000000) {
      strcat( szRating, "N " );    // Nudity
   }
   if (mpaarating & 0x20000000) {
      strcat( szRating, "RP " );   // Rape
   }
   if (mpaarating & 0x40000000) {
      strcat( szRating, "SC " );   // Sexual Content
   }
   if (mpaarating & 0x80000000) {
      strcat( szRating, "V " );    // Violence
   }
   
   *ratingStr = szRating;
}

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
   strReplayShow->channel_id   = ntohl(strReplayShow->channel_id);
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
   strReplayShow->show_id = ntohl(strReplayShow->show_id);
   strReplayShow->seconds = ntohl(strReplayShow->seconds);
   strReplayShow->timessent = ntohl(strReplayShow->timessent);
   strReplayShow->upgradeflag = ntohl(strReplayShow->upgradeflag);
   strReplayShow->unused = ntohs(strReplayShow->unused);
   return;
}

//-------------------------------------------------------------------------
static void convertV2ReplayChannelEndian(replay_channel_v2_t *strReplayChannel)
{
   strReplayChannel->created = ntohl(strReplayChannel->created);
   strReplayChannel->timereserved = ntohll(strReplayChannel->timereserved);
   strReplayChannel->allocatedspace = ntohll(strReplayChannel->allocatedspace);
   strReplayChannel->category = ntohl(strReplayChannel->category);
   strReplayChannel->channeltype = ntohl(strReplayChannel->channeltype);
   strReplayChannel->keep = ntohl(strReplayChannel->keep);
   strReplayChannel->stored = ntohl(strReplayChannel->stored);
   strReplayChannel->quality = ntohl(strReplayChannel->quality);
   return;
}

//-------------------------------------------------------------------------
static void convertV1ReplayChannelEndian(replay_channel_v1_t *strReplayChannel)
{
   strReplayChannel->created = ntohl(strReplayChannel->created);
   strReplayChannel->timereserved = ntohll(strReplayChannel->timereserved);
   strReplayChannel->allocatedspace = ntohll(strReplayChannel->allocatedspace);
   strReplayChannel->category = ntohl(strReplayChannel->category);
   strReplayChannel->channeltype = ntohl(strReplayChannel->channeltype);
   strReplayChannel->keep = ntohl(strReplayChannel->keep);
   strReplayChannel->stored = ntohl(strReplayChannel->stored);
   strReplayChannel->quality = ntohl(strReplayChannel->quality);
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
   char *genre_str, *genre;

   convertReplayShowEndian(show_rec);
   convertProgramInfoEndian(&(show_rec->programInfo));
   convertChannelInfoEndian(&(show_rec->channelInfo));

   sh->show_id    = show_rec->show_id;
   sh->channel_id = show_rec->channel_id;
   
   bufptr = show_rec->programInfo.szDescription;
   
   if ( RTVLOG_GUIDE ) {
      rtv_hex_dump("SHOW_DUMP", 0, (char*)show_rec, sizeof(replay_show_t), 1);
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
      
      sh->rating = xref_as_mask_string(movie->mpaa, mpaa_rating);
      mapExtendedMPAARating(show_rec->programInfo.flags, &(sh->rating_extended));
      sh->movie_stars = movie->stars / 10;
      sh->movie_year = movie->year;
      sh->movie_runtime = ((movie->runtime / 100) * 60) + (movie->runtime % 100);

      RTV_DBGLOG(RTVLOG_GUIDE, "Parse movie_info_t:\n");
      RTV_DBGLOG(RTVLOG_GUIDE, "    mpaa: %d\n", movie->mpaa);
      RTV_DBGLOG(RTVLOG_GUIDE, "    stars: %d\n", movie->stars);
      RTV_DBGLOG(RTVLOG_GUIDE, "    year: %d\n", movie->year);
      RTV_DBGLOG(RTVLOG_GUIDE, "    runtime: %d\n", movie->runtime);
   }
   else {
      // Do TV Ratings
      //
      sh->rating = xref_as_mask_string(show_rec->programInfo.flags, tv_rating);
      mapExtendedTVRating(show_rec->programInfo.flags, &(sh->rating_extended));
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
   sh->title = malloc(show_rec->programInfo.titleLen + 1);
   strncpy(sh->title, convertCodepage(bufptr), show_rec->programInfo.titleLen);
   bufptr += show_rec->programInfo.titleLen;

   sh->episode = malloc(show_rec->programInfo.episodeLen + 1);
   strncpy(sh->episode, convertCodepage(bufptr), show_rec->programInfo.episodeLen);
   bufptr += show_rec->programInfo.episodeLen;
   
   sh->description = malloc(show_rec->programInfo.descriptionLen + 1);
   strncpy(sh->description, convertCodepage(bufptr), show_rec->programInfo.descriptionLen);
   bufptr += show_rec->programInfo.descriptionLen;
    
   sh->actors = malloc(show_rec->programInfo.actorLen + 1);
   strncpy(sh->actors, convertCodepage(bufptr), show_rec->programInfo.actorLen);
   bufptr += show_rec->programInfo.actorLen;
   
   sh->guest = malloc(show_rec->programInfo.guestLen + 1);
   strncpy(sh->guest, convertCodepage(bufptr), show_rec->programInfo.guestLen);
   bufptr += show_rec->programInfo.guestLen;
  
   sh->suzuki = malloc(show_rec->programInfo.suzukiLen + 1);
   strncpy(sh->suzuki, convertCodepage(bufptr), show_rec->programInfo.suzukiLen);
   bufptr += show_rec->programInfo.suzukiLen;
   
   sh->producer = malloc(show_rec->programInfo.producerLen + 1);
   strncpy(sh->producer, convertCodepage(bufptr), show_rec->programInfo.producerLen);
   bufptr += show_rec->programInfo.producerLen;
   
   sh->director = malloc(show_rec->programInfo.directorLen + 1);
   strncpy(sh->director, convertCodepage(bufptr), show_rec->programInfo.directorLen);
   bufptr += show_rec->programInfo.directorLen;

   // mpg filename, gop count, durartion
   //
   sh->file_name = malloc(20);
   snprintf(sh->file_name, 19, "%lu.mpg", show_rec->show_id);
   sh->gop_count       = show_rec->GOP_count;
   sh->duration_sec    = show_rec->seconds;
   sh->duration_str    = rtv_sec_to_hr_mn_str(show_rec->seconds);
   sh->padding_before  = show_rec->beforepadding;
   sh->sch_start_time  = show_rec->programInfo.eventtime;
   sh->sch_show_length = show_rec->programInfo.minutes;
   sh->sch_st_tm_str   = rtv_format_datetime_sec32(sh->sch_start_time);

   // If a shows GOP_count or seconds field is zero then the show has been deleted 
   // but is still in the guide.
   //
   if ( (show_rec->GOP_count == 0) || (show_rec->seconds == 0) ) {
      sh->unavailable = 1;
   }

   // Misc show info
   //
   sh->quality      = show_rec->quality;
   sh->input_source = show_rec->inputsource;
   if ( show_rec->channelInfo.isvalid && show_rec->channelInfo.usetuner ) { 
      sh->tuning = show_rec->programInfo.tuning;
      strncpy(sh->tune_chan_name, show_rec->channelInfo.szChannelName, 15);
      sh->tune_chan_name[15] = '\0';
      strncpy(sh->tune_chan_desc, show_rec->channelInfo.szChannelLabel, 31);
      sh->tune_chan_desc[31] = '\0';
   }
      
   genre_str = malloc(GENRE_STR_SZ+1);
   genre_str[0] = '\0';
   genre = xref_string(show_rec->programInfo.genre1, show_genre);
   if ( genre[0] != '\0' ) {
      strncpy(genre_str, genre, GENRE_STR_SZ);
      strncat(genre_str, " ", GENRE_STR_SZ);
   }
   genre = xref_string(show_rec->programInfo.genre2, show_genre);
   if ( genre[0] != '\0' ) {
      strncat(genre_str, genre, GENRE_STR_SZ);
      strncat(genre_str, " ", GENRE_STR_SZ);
   }
   genre = xref_string(show_rec->programInfo.genre3, show_genre);
   if ( genre[0] != '\0' ) {
      strncat(genre_str, genre, GENRE_STR_SZ);
      strncat(genre_str, " ", GENRE_STR_SZ);
   }
   genre = xref_string(show_rec->programInfo.genre4, show_genre);
   if ( genre[0] != '\0' ) {
      strncat(genre_str, genre, GENRE_STR_SZ);
   }
   sh->genre = genre_str;


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
   if (sizeof(replay_channel_v2_t) != REPLAYCHANNEL_SZ) {
      RTV_ERRLOG("replay_channel_t structure, needs to be %u instead of %u\n",REPLAYCHANNEL_SZ,sizeof(replay_channel_v2_t));
      rc = -1;
   }
   if (sizeof(guide_header_t) != GUIDEHEADER_SZ) {
      RTV_ERRLOG("guide_header_t structure, needs to be %u instead of %u\n",GUIDEHEADER_SZ,sizeof(guide_header_t));
      rc = -1;
   }
   if (sizeof(v1_guide_header_t) != GUIDEHEADER_V1_SZ) {
      RTV_ERRLOG("v1_guide_header_t structure, needs to be %u instead of %u\n",GUIDEHEADER_V1_SZ,sizeof(v1_guide_header_t));
      rc = -1;
   }
   return(rc);
}

//+***********************************************************************************************
//  Name: convert_v1_to_v2_snapshotheader
//+***********************************************************************************************
static int convert_v1_to_v2_snapshot_header(guide_header_t *v2hdr, v1_guide_header_t *v1hdr) 
{
   __u32 chancount;
   memset(v2hdr, 0, sizeof(guide_header_t));
   v2hdr->guideSnapshotHeader.osversion         = htons(0);
   v2hdr->guideSnapshotHeader.snapshotversion   = htons(2);
   v2hdr->guideSnapshotHeader.structuresize     = htonl(64);
   v2hdr->guideSnapshotHeader.channelcount      = v1hdr->guideSnapshotHeader.channelcount;
   v2hdr->guideSnapshotHeader.channelcountcheck = v1hdr->guideSnapshotHeader.channelcountcheck;
   v2hdr->guideSnapshotHeader.groupdataoffset   = htonl(64);
   v2hdr->guideSnapshotHeader.channeloffset     = htonl(808); //offset from v1 header start
   chancount                                    = ntohl(v1hdr->guideSnapshotHeader.channelcount);
   v2hdr->guideSnapshotHeader.showoffset        = htonl(808 + (V1_CHAN_REC_SIZE * chancount)); //offset from v1 header start
   v2hdr->guideSnapshotHeader.flags             = v1hdr->guideSnapshotHeader.flags;

   memcpy(&(v2hdr->groupData), &(v1hdr->groupData), sizeof(group_data_t));
   return(0);
}

//+***********************************************************************************
//                         PUBLIC FUNCTIONS
//+***********************************************************************************

int parse_guide_snapshot(const char *guideDump, int size, rtv_guide_export_t *guideExport)
{
   guide_header_t             *guidehdr       = (guide_header_t*)guideDump;
   v2_guide_snapshot_header_t *sshdr          = &(guidehdr->guideSnapshotHeader);
   group_data_t               *gdatahdr       = &(guidehdr->groupData);
   rtv_channel_export_t      **rtvChanExportP = &(guideExport->channel_list);
   rtv_show_export_t         **rtvShowExportP = &(guideExport->rec_show_list);

   rtv_guide_ver_t             gver;
   guide_header_t              v2hdr;
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

   // Determine guide version.
   //
   if ( (sshdr->osversion == ntohs(0)) && (sshdr->snapshotversion == ntohs(2)) ) { 
      RTV_DBGLOG(RTVLOG_GUIDE, "Processing Version 2 Guide\n");
      gver = RTV_GV_2; //5K
   }
   else if ( (sshdr->osversion == ntohs(3)) && (sshdr->snapshotversion == ntohs(1)) ) {
      v1_guide_header_t *v1hdr = (v1_guide_header_t*)guideDump;
      RTV_DBGLOG(RTVLOG_GUIDE, "Processing Version 1 Guide\n");
      gver = RTV_GV_1; //4K
      convert_v1_to_v2_snapshot_header(&v2hdr, v1hdr);
      v2hdr.guideSnapshotHeader.snapshotsize = htonl(size);
      guidehdr = &v2hdr;
      sshdr    = &(v2hdr.guideSnapshotHeader);
      gdatahdr = &(v2hdr.groupData);
   }
   else {
      RTV_ERRLOG("Guide file is either damaged or from an unsupported ReplayTV OS Version\n");
      RTV_ERRLOG("    osver.snapshotver=%d.%d   shected=0.2\n", sshdr->osversion, sshdr->snapshotversion);
      print_v2_guide_snapshot_header(sshdr);
      return(-1);
   }

   convertReplayGuideEndian(guidehdr);

   // guide header error checking & some setup.
   //
   if (sshdr->channelcount != sshdr->channelcountcheck) {
      RTV_ERRLOG("channelcount mismatch: Guide Snapshot might be damaged\n");
      print_v2_guide_snapshot_header(sshdr);
      return(-1);
   }

   memAllocShows = sshdr->snapshotsize - sshdr->showoffset;
   if ( gver == RTV_GV_1 ) {
      if ( (memAllocShows % sizeof(replay_show_t)) != 0 ) { 
         RTV_ERRLOG("Invalid V1 show storage size: allocated=%u show_sz=%u\n", memAllocShows, sizeof(replay_show_t));
         print_v2_guide_snapshot_header(sshdr);
         return(-1);
      }
      numShows = memAllocShows / sizeof(replay_show_t);
   }
   else {
      if ( (memAllocShows % (sizeof(replay_show_t) + REPLAYSHOW_V2_PADDING)) != 0 ) { 
         RTV_ERRLOG("Invalid show storage size: allocated=%u show_sz=%u\n", memAllocShows, sizeof(replay_show_t) + REPLAYSHOW_V2_PADDING);
         print_v2_guide_snapshot_header(sshdr);
         return(-1);
      }
      numShows = memAllocShows / (sizeof(replay_show_t) + REPLAYSHOW_V2_PADDING);
   }

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

   // Process Replay Channels
   //
   *rtvChanExportP = malloc(sizeof(rtv_channel_export_t) * sshdr->channelcount);   
   //
   // JBH: TODO: Parse rest of channel info. Not really needed for anything right now.
   //
   if ( gver == RTV_GV_1 ) {
      replay_channel_v1_t  *chan_rec   = (replay_channel_v1_t*)(guideDump + sshdr->channeloffset);
      rtv_channel_export_t *chanExport = *rtvChanExportP;       
      for( x = 0 ; x < sshdr->channelcount; x++ ) {
         convertV1ReplayChannelEndian(chan_rec);
         convertReplayShowEndian(&(chan_rec->replayShow));
         convertThemeInfoEndian(&(chan_rec->themeInfo));
         chanExport[x].channel_id   = chan_rec->created;
         mapDaysOfWeek(chan_rec->daysofweek, &(chanExport[x].days_of_week));   
      }      
   }
   else {
      replay_channel_v2_t  *chan_rec   = (replay_channel_v2_t*)(guideDump + sshdr->channeloffset);
      rtv_channel_export_t *chanExport = *rtvChanExportP;       
      for( x = 0 ; x < sshdr->channelcount; x++ ) {
         convertV2ReplayChannelEndian(chan_rec);
         convertReplayShowEndian(&(chan_rec->replayShow));
         convertThemeInfoEndian(&(chan_rec->themeInfo));
         chanExport[x].channel_id   = chan_rec->created;
         mapDaysOfWeek(chan_rec->daysofweek, &(chanExport[x].days_of_week));   
      }      
   }

   // Process the recorded shows
   //
   show_rec        = (replay_show_t*)(guideDump + sshdr->showoffset);
   *rtvShowExportP = malloc(sizeof(rtv_show_export_t) * numShows);
   rtvShowExport   = *rtvShowExportP;
   memset(rtvShowExport, 0, sizeof(rtv_show_export_t) * numShows);

   RTV_DBGLOG(RTVLOG_GUIDE, "show array base address:   %p    num_shows=%d\n", show_rec, numShows); 
   for( x = 0 ; x < numShows; x++ ) {
      replay_show_t *show_p;
      if ( gver == RTV_GV_1 ) {
         show_p = &(show_rec[x]); 
      }
      else {
         show_p = (replay_show_t*)( (char*)&(show_rec[0]) + ((sizeof(replay_show_t) + REPLAYSHOW_V2_PADDING) * x) );
      }
      RTV_DBGLOG(RTVLOG_GUIDE, "show[%u] struct base address: %p\n", x, show_p); 
      if ( (rc = parse_show( show_p, &(rtvShowExport[x]))) != 0 ) {
         return(rc);
      }
      if ( RTVLOG_GUIDE ) {
         rtv_print_show(&(rtvShowExport[x]), x);
      }
   }

   guideExport->num_rec_shows = numShows;
   return(0);
}


//+********************************************************************************
// build_v2_bogus_snapshot:
// Makes a RTV 5K (5.0 OS Version) snapshot.
// This is used to make DVArchive go into RTV 5K mode
// DVArchive expects a minimum of the snapshot header, group data and one channel. 
//+********************************************************************************
int build_v2_bogus_snapshot( char **snapshot )
{
   guide_header_t      *ss;
   replay_channel_v2_t *chan;

   ss = malloc(sizeof(v2_guide_snapshot_header_t) + sizeof(group_data_t) + sizeof(replay_channel_v2_t));
   memset(ss, 0, sizeof(v2_guide_snapshot_header_t) + sizeof(group_data_t) + sizeof(replay_channel_v2_t));
   ss->guideSnapshotHeader.osversion         = htons(0);
   ss->guideSnapshotHeader.snapshotversion   = htons(2);
   ss->guideSnapshotHeader.structuresize     = htonl(sizeof(v2_guide_snapshot_header_t));
   ss->guideSnapshotHeader.unknown1          = htonl(2);
   ss->guideSnapshotHeader.unknown2          = htonl(5);
   ss->guideSnapshotHeader.channelcount      = htonl(1);
   ss->guideSnapshotHeader.channelcountcheck = htonl(1);
   ss->guideSnapshotHeader.groupdataoffset   = htonl(sizeof(v2_guide_snapshot_header_t));
   ss->guideSnapshotHeader.channeloffset     = htonl(sizeof(v2_guide_snapshot_header_t) + sizeof(group_data_t));
   ss->guideSnapshotHeader.showoffset        = htonl(sizeof(v2_guide_snapshot_header_t) + sizeof(group_data_t) + sizeof(replay_channel_v2_t));
   ss->guideSnapshotHeader.snapshotsize      = htonl(sizeof(v2_guide_snapshot_header_t) + sizeof(group_data_t) + sizeof(replay_channel_v2_t));

   ss->groupData.structuresize = htonl(sizeof(group_data_t));

   chan = (replay_channel_v2_t*)((char*)ss + sizeof(v2_guide_snapshot_header_t) + sizeof(group_data_t));
   chan->created     = htonl(0x00012345);
   chan->channeltype =  htonl(1);
   strcpy(chan->szShowLabel, "MVPMC_Bogus_5K_Channel");

   *snapshot = (char*)ss;
   return(sizeof(v2_guide_snapshot_header_t) + sizeof(group_data_t) + sizeof(replay_channel_v2_t));
}

//+********************************************************************************
// build_v1_bogus_snapshot:
// Makes a RTV 4K (3.1 OS Version) snapshot.
// This is used to make DVArchive go into RTV 4K mode
// DVArchive expects a minimum of the snapshot header & group data. 
//+********************************************************************************
int build_v1_bogus_snapshot( char **snapshot )
{
   v1_guide_header_t *ss;

   ss = malloc(sizeof(v1_guide_snapshot_header_t) + sizeof(group_data_t));
   memset(ss, 0, sizeof(v1_guide_snapshot_header_t) + sizeof(group_data_t));
   ss->guideSnapshotHeader.osversion         = htons(3);
   ss->guideSnapshotHeader.snapshotversion   = htons(1);
   ss->guideSnapshotHeader.structuresize     = htonl(sizeof(v1_guide_snapshot_header_t));
   ss->guideSnapshotHeader.channelcount      = htonl(0);
   ss->guideSnapshotHeader.channelcountcheck = htonl(0);
   ss->guideSnapshotHeader.groupdataoffset   = htonl(sizeof(v1_guide_snapshot_header_t));
   ss->guideSnapshotHeader.channeloffset     = htonl(sizeof(v1_guide_snapshot_header_t) + sizeof(group_data_t));
   ss->guideSnapshotHeader.showoffset        = htonl(sizeof(v1_guide_snapshot_header_t) + sizeof(group_data_t));

   ss->groupData.structuresize = htonl(sizeof(group_data_t));

   *snapshot = (char*)ss;
   return(sizeof(v1_guide_snapshot_header_t) + sizeof(group_data_t));
}

