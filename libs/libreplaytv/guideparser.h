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
   u16 osversion;            // OS Version (5 for 4.5, 3 for 4.3, 0 on 5.0)
   u16 snapshotversion;      // Snapshot Version (1) (2 on 5.0)    This might be better termed as snapshot version
   u32 structuresize;        // Should always be 64
   u32 unknown1;             // 0x00000002
   u32 unknown2;             // 0x00000005
   u32 channelcount;         // Number of Replay Channels
   u32 channelcountcheck;    // Should always be equal to channelcount, it's incremented as the snapshot is built.
   u32 unknown3;             // 0x00000000
   u32 groupdataoffset;      // Offset of Group Data
   u32 channeloffset;        // Offset of First Replay Channel  (If you don't care about categories, jump to this)
   u32 showoffset;           // Offset of First Replay Show (If you don't care about ReplayChannels, jump to this)
   u32 snapshotsize;         // Total Size of the Snapshot
   u64 bytesfree;            // Total size in bytes free on rtv
   u32 flags;                // Careful, this is uninitialized, ignore invalid bits
   u32 unknown6;             // 0x00000002
   u32 unknown7;             // 0x00000000
} v2_guide_snapshot_header_t; 


#define GRP_DATA_NUM_CATEGORIES 32
#define GRP_DATA_CATBUF_SZ      512
typedef struct group_data_t {
    u32  structuresize;                            // Should always be 776
    u32  categories;                               // Number of Categories (MAX: 32 [ 0 - 31 ] )
    u32  category[GRP_DATA_NUM_CATEGORIES];        // category lookup 2 ^ number, position order = text
    u32  categoryoffset[GRP_DATA_NUM_CATEGORIES];  // Offsets for the GuideHeader.catbuffer
    u8   catbuffer[GRP_DATA_CATBUF_SZ];            // GuideHeader.categoryoffset contains the starting position of each category.
}  group_data_t;


#define GUIDEHEADER_SZ     840    // needed size of guideheader structure
typedef struct guide_header_t {
    struct  v2_guide_snapshot_header_t guideSnapshotHeader;
    struct  group_data_t               groupData;
} guide_header_t;


#define THEMEINFO_V1_PADDING (4)
typedef struct theme_info_t {
    u32  flags;               // Search Flags
    u32  suzuki_id;           // Suzuki ID
    u32  thememinutes;        // Minutes Allocated
    char szSearchString[48];  // Search Text     (52 bytes for V1 snapshot)
} theme_info_t;

typedef struct movie_info_t {
    u16 mpaa;          // MPAA Rating
    u16 stars;         // Star Rating * 10  (i.e. value of 20 = 2 stars)
    u16 year;          // Release Year
    u16 runtime;       // Strange HH:MM format
} movie_info_t;


typedef struct parts_info_t {
    u16 partnumber;       // Part X [Of Y]
    u16 maxparts;         // [Part X] Of Y
} parts_info_t;


typedef struct program_info_t {
    u32  structuresize;       // Should always be 272 (0x0110)
    u32  autorecord;          // If non-zero it is an automatic recording, otherwise it is a manual recording.
    u32  isvalid;             // Not sure what it actually means! (should always be 1 in exported guide)
    u32  tuning;              // Tuning (Channel Number) for the Program
    u32  flags;               // Program Flags
    u32  eventtime;           // Scheduled Time of the Show
    u32  tmsID;               // Tribune Media Services ID (inherited from ChannelInfo)
    u16  minutes;             // Minutes (add with show padding for total)
    u8   genre1;              // Genre Code 1
    u8   genre2;              // Genre Code 2
    u8   genre3;              // Genre Code 3
    u8   genre4;              // Genre Code 4
    u16  recLen;              // Record Length of Description Block
    u8   titleLen;            // Length of Title
    u8   episodeLen;          // Length of Episode
    u8   descriptionLen;      // Length of Description
    u8   actorLen;            // Length of Actors
    u8   guestLen;            // Length of Guest
    u8   suzukiLen;           // Length of Suzuki String (Newer genre tags)
    u8   producerLen;         // Length of Producer
    u8   directorLen;         // Length of Director
    u8   szDescription[228];  // This can have parts/movie sub-structure
} program_info_t;

typedef struct channel_info_t {
    u32 structuresize;        // Should always be 80 (0x50)
    u32 usetuner;             // If non-zero the tuner is used, otherwise it's a direct input.
    u32 isvalid;              // Record valid if non-zero
    u32 tmsID;                // Tribune Media Services ID
    u16 channel;              // Channel Number
    u8 device;                // Device
    u8 tier;                  // Cable/Satellite Service Tier
    char szChannelName[16];   // Channel Name
    char szChannelLabel[32];  // Channel Description
    char cablesystem[8];      // Cable system ID
    u32 channelindex;         // Channel Index (USUALLY tagChannelinfo.channel repeated)
} channel_info_t;


#define REPLAYSHOW_V2_PADDING (68)     // V2 guide bytes of padding at end of show structure
typedef struct replay_show_t {
    u32 created;              // ReplayChannel ID (tagReplayChannel.created)
    u32 recorded;             // Filename/Time of Recording (aka ShowID)
    u32 inputsource;          // Replay Input Source
    u32 quality;              // Recording Quality Level
    u32 guaranteed;           // (0xFFFFFFFF if guaranteed)
    u32 playbackflags;        // Not well understood yet.
    channel_info_t channelInfo;
    program_info_t programInfo;
    u32 ivsstatus;            // Always 1 in a snapshot outside of a tagReplayChannel
    u32 guideid;              // The show_id on the original ReplayTV for IVS shows.   Otherwise 0
    u32 downloadid;           // Valid only during actual transfer of index or mpeg file; format/meaning still unknown
    u32 timessent;            // Times sent using IVS
    u32 seconds;              // Show Duration in Seconds (this is the exact actual length of the recording)
    u32 GOP_count;            // MPEG Group of Picture Count
    u32 GOP_highest;          // Highest GOP number seen, 0 on a snapshot.
    u32 GOP_last;             // Last GOP number seen, 0 on a snapshot.
    u32 checkpointed;         // 0 in possibly-out-of-date in-memory copies; always -1 in snapshots
    u32 intact;               // 0xffffffff in a snapshot; 0 means a deleted show
    u32 upgradeflag;          // Always 0 in a snapshot
    u32 instance;             // Episode Instance Counter (0 offset)
    u16 unused;               // Not preserved when padding values are set, presumably not used
    u8 beforepadding;         // Before Show Padding
    u8 afterpadding;          // After Show Padding
    u64 indexsize;            // Size of NDX file (IVS Shows Only)
    u64 mpegsize;             // Size of MPG file (IVS Shows Only)
} replay_show_t;

typedef struct replay_channel_v2_t {
   replay_show_t replayShow;
   u8     v2show_padding[REPLAYSHOW_V2_PADDING];
   theme_info_t  themeInfo;
   u32   created;            // Timestamp Entry Created
   u32   category;           // 2 ^ Category Number
   u32   channeltype;        // Channel Type (Recurring, Theme, Single)
   u32   quality;            // Recording Quality (High, Medium, Low)
   u32   stored;             // Number of Episodes Recorded
   u32   keep;               // Number of Episodes to Keep
   u8    daysofweek;         // Day of the Week to Record Bitmask (Sun = LSB and Sat = MSB)
   u8    afterpadding;       // End of Show Padding
   u8    beforepadding;      // Beginning of Show Padding
   u8    flags;              // ChannelFlags
   u64   timereserved;       // Total Time Allocated (For Guaranteed Shows)
   char  szShowLabel[48];    // Show Label
   u32   unknown1;           // Unknown (Reserved For Don array)
   u32   unknown2;           // Unknown
   u32   unknown3;           // Unknown
   u32   unknown4;           // Unknown
   u32   unknown5;           // Unknown
   u32   unknown6;           // Unknown
   u32   unknown7;           // Unknown
   u32   unknown8;           // Unknown
   u64   allocatedspace;     // Total Space Allocated
   u32   unknown9;           // Unknown
   u32   unknown10;          // Unknown
   u32   unknown11;          // Unknown
   u32   unknown12;          // Unknown
} replay_channel_v2_t;


typedef struct replay_channel_v1_t {
   u32   channeltype;        // Channel Type (Recurring, Theme, Single)
   u32   quality;            // Recording Quality (High, Medium, Low)
   u64   allocatedspace;     // Total Space Allocated
   u32   keep;               // Number of Episodes to Keep
   u32   stored;             // Number of Episodes Recorded
   u8    daysofweek;         // Day of the Week to Record Bitmask (Sun = LSB and Sat = MSB)
   u8    afterpadding;       // End of Show Padding
   u8    beforepadding;      // Beginning of Show Padding
   u8    flags;              // ChannelFlags
   u32   category;           // 2 ^ Category Number
   u32   created;            // Timestamp Entry Created
   u32   unknown1;           // Unknown (Reserved For Don array)
   u32   unknown2;           // Unknown
   u32   unknown3;           // Unknown
   u32   unknown4;           // Unknown
   u32   unknown5;           // Unknown
   u32   unknown6;           // Unknown
   u32   unknown7;           // Unknown
   u64   timereserved;       // Total Time Allocated (For Guaranteed Shows)
   char  szShowLabel[48];    // Show Label
   replay_show_t replayShow;
   theme_info_t  themeInfo;
   u8     v2show_padding[THEMEINFO_V1_PADDING];
} replay_channel_v1_t;

typedef struct category_array_t {
   u32   index;            // Index Value
   char  szName[20];       // Category Text
} category_array_t;


typedef struct  channel_array_t {
   u32   channel_id;          // ReplayTV Channel ID
   char  szCategory[10];      // Category Text
   u32   keep;                // Number of Episodes to Keep
   u32   stored;              // Number of Episodes Recorded Thus Far
   u32   channeltype;         // Channel Type
   char  szChannelType[16];   // Channel Type (Text)
   char  szShowLabel[48];     // Show Label
} channel_array_t;


// 4K OSVersion 4.3
typedef struct v1_guide_snapshot_header_t { 
    u16 osversion;          // Major Revision (3 for Replay 4000/4500s)
    u16 snapshotversion;    // Minor Revision (1)
    u32 structuresize;      // Should always be 32
    u32 channelcount;       // Number of Replay Channels
    u32 channelcountcheck;  // Number of Replay Channels (Copy 2; should always match)
    u32 groupdataoffset;    // Offset of Group Data
    u32 channeloffset;      // Offset of First Replay Channel
    u32 showoffset;         // Offset of First Replay Show
    u32 flags;              // this is uninitialized, ignore invalid bits
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
