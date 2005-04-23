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


#ifndef __GUIDEPARSER_H__
#define __GUIDEPARSER_H__

#define REPLAYSHOW_SZ      444    // needed size of replayshow structure
#define REPLAYCHANNEL_SZ   712    // needed size of replaychannel structure
#define PROGRAMINFO_SZ     272    // needed size of programinfo structure
#define CHANNELINFO_SZ     80     // needed size of channelinfo structure


typedef enum day_of_week_t
{
   CE_SUN =      1<<0,
   CE_MON =      1<<1,
   CE_TUE =      1<<2,
   CE_WED =      1<<3,
   CE_THU =      1<<4,
   CE_FRI =      1<<5,
   CE_SAT =      1<<6,
   CE_UNKNOWN =  1<<7
} day_of_week_t;


#pragma pack(1)  // byte alignment

// RTV 5K OSVersion 5.0 
//
typedef struct v2_guide_snapshot_header_t { 
   __u16 osversion;            // OS Version (5 for 4.5, 3 for 4.3, 0 on 5.0)
   __u16 snapshotversion;      // Snapshot Version (1) (2 on 5.0)    This might be better termed as snapshot version
   __u32 structuresize;        // Should always be 64
   __u32 unknown1;             // 0x00000002
   __u32 unknown2;             // 0x00000005
   __u32 channelcount;         // Number of Replay Channels
   __u32 channelcountcheck;    // Should always be equal to channelcount, it's incremented as the snapshot is built.
   __u32 unknown3;             // 0x00000000
   __u32 groupdataoffset;      // Offset of Group Data
   __u32 channeloffset;        // Offset of First Replay Channel  (If you don't care about categories, jump to this)
   __u32 showoffset;           // Offset of First Replay Show (If you don't care about ReplayChannels, jump to this)
   __u32 snapshotsize;         // Total Size of the Snapshot
   __u64 bytesfree;            // Total size in bytes free on rtv
   __u32 flags;                // Careful, this is uninitialized, ignore invalid bits
   __u32 unknown6;             // 0x00000002
   __u32 unknown7;             // 0x00000000
} v2_guide_snapshot_header_t; 


#define GRP_DATA_NUM_CATEGORIES 32
#define GRP_DATA_CATBUF_SZ      512
typedef struct group_data_t {
    __u32  structuresize;                            // Should always be 776
    __u32  categories;                               // Number of Categories (MAX: 32 [ 0 - 31 ] )
    __u32  category[GRP_DATA_NUM_CATEGORIES];        // category lookup 2 ^ number, position order = text
    __u32  categoryoffset[GRP_DATA_NUM_CATEGORIES];  // Offsets for the GuideHeader.catbuffer
    __u8   catbuffer[GRP_DATA_CATBUF_SZ];            // GuideHeader.categoryoffset contains the starting position of each category.
}  group_data_t;


#define GUIDEHEADER_SZ     840    // needed size of guideheader structure
typedef struct guide_header_t {
    struct  v2_guide_snapshot_header_t guideSnapshotHeader;
    struct  group_data_t               groupData;
} guide_header_t;


#define THEMEINFO_V1_PADDING (4)
typedef struct theme_info_t {
    __u32  flags;               // Search Flags
    __u32  suzuki_id;           // Suzuki ID
    __u32  thememinutes;        // Minutes Allocated
    char   szSearchString[48];  // Search Text     (52 bytes for V1 snapshot)
} theme_info_t;

typedef struct movie_info_t {
    __u16 mpaa;          // MPAA Rating
    __u16 stars;         // Star Rating * 10  (i.e. value of 20 = 2 stars)
    __u16 year;          // Release Year
    __u16 runtime;       // Strange HH:MM format
} movie_info_t;


typedef struct parts_info_t {
    __u16 partnumber;       // Part X [Of Y]
    __u16 maxparts;         // [Part X] Of Y
} parts_info_t;


typedef struct program_info_t {
    __u32  structuresize;       // Should always be 272 (0x0110)
    __u32  autorecord;          // If non-zero it is an automatic recording, otherwise it is a manual recording.
    __u32  isvalid;             // Not sure what it actually means! (should always be 1 in exported guide)
    __u32  tuning;              // Tuning (Channel Number) for the Program
    __u32  flags;               // Program Flags
    __u32  eventtime;           // Scheduled Time of the Show
    __u32  tmsID;               // Tribune Media Services ID (inherited from ChannelInfo)
    __u16  minutes;             // Minutes (add with show padding for total)
    __u8   genre1;              // Genre Code 1
    __u8   genre2;              // Genre Code 2
    __u8   genre3;              // Genre Code 3
    __u8   genre4;              // Genre Code 4
    __u16  recLen;              // Record Length of Description Block
    __u8   titleLen;            // Length of Title
    __u8   episodeLen;          // Length of Episode
    __u8   descriptionLen;      // Length of Description
    __u8   actorLen;            // Length of Actors
    __u8   guestLen;            // Length of Guest
    __u8   suzukiLen;           // Length of Suzuki String (Newer genre tags)
    __u8   producerLen;         // Length of Producer
    __u8   directorLen;         // Length of Director
    __u8   szDescription[228];  // This can have parts/movie sub-structure
} program_info_t;

typedef struct channel_info_t {
    __u32 structuresize;        // Should always be 80 (0x50)
    __u32 usetuner;             // If non-zero the tuner is used, otherwise it's a direct input.
    __u32 isvalid;              // Record valid if non-zero
    __u32 tmsID;                // Tribune Media Services ID
    __u16 channel;              // Channel Number
    __u8  device;               // Device
    __u8  tier;                 // Cable/Satellite Service Tier
    char  szChannelName[16];    // Channel Name
    char  szChannelLabel[32];   // Channel Description
    char  cablesystem[8];       // Cable system ID
    __u32 channelindex;         // Channel Index (USUALLY tagChannelinfo.channel repeated)
} channel_info_t;


#define REPLAYSHOW_V2_PADDING (68)     // V2 guide bytes of padding at end of show structure
typedef struct replay_show_t {
    __u32 channel_id;           // ReplayChannel ID (tagReplayChannel.created)
    __u32 show_id;              // Filename/Time of Recording (aka ShowID)
    __u32 inputsource;          // Replay Input Source
    __u32 quality;              // Recording Quality Level
    __u32 guaranteed;           // (0xFFFFFFFF if guaranteed)
    __u32 playbackflags;        // Not well understood yet.
    channel_info_t channelInfo;
    program_info_t programInfo;
    __u32 ivsstatus;            // Always 1 in a snapshot outside of a tagReplayChannel
    __u32 guideid;              // The show_id on the original ReplayTV for IVS shows.   Otherwise 0
    __u32 downloadid;           // Valid only during actual transfer of index or mpeg file; format/meaning still unknown
    __u32 timessent;            // Times sent using IVS
    __u32 seconds;              // Show Duration in Seconds (this is the exact actual length of the recording)
    __u32 GOP_count;            // MPEG Group of Picture Count
    __u32 GOP_highest;          // Highest GOP number seen, 0 on a snapshot.
    __u32 GOP_last;             // Last GOP number seen, 0 on a snapshot.
    __u32 checkpointed;         // 0 in possibly-out-of-date in-memory copies; always -1 in snapshots
    __u32 intact;               // 0xffffffff in a snapshot; 0 means a deleted show
    __u32 upgradeflag;          // Always 0 in a snapshot
    __u32 instance;             // Episode Instance Counter (0 offset)
    __u16 unused;               // Not preserved when padding values are set, presumably not used
    __u8 beforepadding;         // Before Show Padding
    __u8 afterpadding;          // After Show Padding
    __u64 indexsize;            // Size of NDX file (IVS Shows Only)
    __u64 mpegsize;             // Size of MPG file (IVS Shows Only)
} replay_show_t;

typedef struct replay_channel_v2_t {
   replay_show_t replayShow;
   __u8     v2show_padding[REPLAYSHOW_V2_PADDING];
   theme_info_t  themeInfo;
   __u32   created;            // Timestamp Entry Created
   __u32   category;           // 2 ^ Category Number
   __u32   channeltype;        // Channel Type (Recurring, Theme, Single)
   __u32   quality;            // Recording Quality (High, Medium, Low)
   __u32   stored;             // Number of Episodes Recorded
   __u32   keep;               // Number of Episodes to Keep
   __u8    daysofweek;         // Day of the Week to Record Bitmask (Sun = LSB and Sat = MSB)
   __u8    afterpadding;       // End of Show Padding
   __u8    beforepadding;      // Beginning of Show Padding
   __u8    flags;              // ChannelFlags
   __u64   timereserved;       // Total Time Allocated (For Guaranteed Shows)
   char    szShowLabel[48];    // Show Label
   __u32   unknown1;           // Unknown (Reserved For Don array)
   __u32   unknown2;           // Unknown
   __u32   unknown3;           // Unknown
   __u32   unknown4;           // Unknown
   __u32   unknown5;           // Unknown
   __u32   unknown6;           // Unknown
   __u32   unknown7;           // Unknown
   __u32   unknown8;           // Unknown
   __u64   allocatedspace;     // Total Space Allocated
   __u32   unknown9;           // Unknown
   __u32   unknown10;          // Unknown
   __u32   unknown11;          // Unknown
   __u32   unknown12;          // Unknown
} replay_channel_v2_t;


typedef struct replay_channel_v1_t {
   __u32   channeltype;        // Channel Type (Recurring, Theme, Single)
   __u32   quality;            // Recording Quality (High, Medium, Low)
   __u64   allocatedspace;     // Total Space Allocated
   __u32   keep;               // Number of Episodes to Keep
   __u32   stored;             // Number of Episodes Recorded
   __u8    daysofweek;         // Day of the Week to Record Bitmask (Sun = LSB and Sat = MSB)
   __u8    afterpadding;       // End of Show Padding
   __u8    beforepadding;      // Beginning of Show Padding
   __u8    flags;              // ChannelFlags
   __u32   category;           // 2 ^ Category Number
   __u32   created;            // Timestamp Entry Created
   __u32   unknown1;           // Unknown (Reserved For Don array)
   __u32   unknown2;           // Unknown
   __u32   unknown3;           // Unknown
   __u32   unknown4;           // Unknown
   __u32   unknown5;           // Unknown
   __u32   unknown6;           // Unknown
   __u32   unknown7;           // Unknown
   __u64   timereserved;       // Total Time Allocated (For Guaranteed Shows)
   char    szShowLabel[48];    // Show Label
   replay_show_t replayShow;
   theme_info_t  themeInfo;
   __u8     v2show_padding[THEMEINFO_V1_PADDING];
} replay_channel_v1_t;

typedef struct category_array_t {
   __u32 index;            // Index Value
   char  szName[20];       // Category Text
} category_array_t;


typedef struct  channel_array_t {
   __u32   channel_id;          // ReplayTV Channel ID
   char    szCategory[10];      // Category Text
   __u32   keep;                // Number of Episodes to Keep
   __u32   stored;              // Number of Episodes Recorded Thus Far
   __u32   channeltype;         // Channel Type
   char    szChannelType[16];   // Channel Type (Text)
   char    szShowLabel[48];     // Show Label
} channel_array_t;


// 4K OSVersion 4.3
typedef struct v1_guide_snapshot_header_t { 
    __u16 osversion;          // Major Revision (3 for Replay 4000/4500s)
    __u16 snapshotversion;    // Minor Revision (1)
    __u32 structuresize;      // Should always be 32
    __u32 channelcount;       // Number of Replay Channels
    __u32 channelcountcheck;  // Number of Replay Channels (Copy 2; should always match)
    __u32 groupdataoffset;    // Offset of Group Data
    __u32 channeloffset;      // Offset of First Replay Channel
    __u32 showoffset;         // Offset of First Replay Show
    __u32 flags;              // this is uninitialized, ignore invalid bits
} v1_guide_snapshot_header_t; 


// 4K OSVersion 4.3
#define GUIDEHEADER_V1_SZ     808    // needed size of 4K guideheader structure
typedef struct v1_guide_header_t {
    struct  v1_guide_snapshot_header_t guideSnapshotHeader;
    struct  group_data_t               groupData;
} v1_guide_header_t;

#pragma pack(4)  // back to normal alignment

//+******************************************************
//+****          Prototypes
//+******************************************************
extern int parse_guide_snapshot(const char *guideDump, int size, rtv_guide_export_t *guideExport);
extern int build_v2_bogus_snapshot(char **snapshot);
extern int build_v1_bogus_snapshot(char **snapshot);

#endif
