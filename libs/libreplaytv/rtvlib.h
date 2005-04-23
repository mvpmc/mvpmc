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

#ifndef __RTVLIB_H__
#define __RTVLIB_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define MAX_RTVS 10

//+**********************************************************
// Define architecture specific data types
//+**********************************************************
#ifndef __RTV_DATA_SIZES_DEFINED__
typedef unsigned char      __u8;
typedef unsigned short     __u16;
typedef unsigned long      __u32;
typedef unsigned long long __u64;
typedef signed   long long __s64;
#endif

//+****************************************
// RTV Filesystem types/ structures
//+****************************************
typedef enum rtv_filesystype_t 
{
   RTV_FS_DIRECTORY = 'd',
   RTV_FS_FILE      = 'f',
   RTV_FS_UNKNOWN   = '?'
} rtv_filesystype_t;

typedef struct rtv_fs_file_t
{
   char              *name;
   rtv_filesystype_t  type;
   __u64              size;
   __u64              time;
   __u32              size_k;
   char              *time_str_fmt1;
   char              *time_str_fmt2;
} rtv_fs_file_t;

typedef struct rtv_fs_filelist_t
{
   char              *pathname;
   unsigned int       num_files;
   rtv_fs_file_t     *files;
} rtv_fs_filelist_t;

typedef struct rtv_fs_volume_t
{
   char      *name;
   __u64      size;
   __u32      size_k;
   __u64      used;
   __u32      used_k;
} rtv_fs_volume_t;

//+****************************************
// RTV Show Information Types
//+****************************************
typedef enum rtv_show_quality_t 
{
   RTV_QUALITY_HIGH     = 0, 
   RTV_QUALITY_MEDIUM   = 1, 
   RTV_QUALITY_STANDARD = 2 
} rtv_show_quality_t;

//+****************************************
// RTV Device Information structure
//+****************************************
typedef enum rtv_vintage_t 
{
   RTV_DEVICE_UNKNOWN = 0,
   RTV_DEVICE_4K      = 4,
   RTV_DEVICE_5K      = 5,
} rtv_vintage_t;

typedef struct rtv_device_version_t 
{
   rtv_vintage_t  vintage;
   int            major;
   int            minor;
   int            build;
} rtv_device_version_t;

typedef struct rtv_device_info_t 
{
   char                 *ipaddr;
   char                 *deviceType; 
   char                 *name;
   char                 *modelDescr; 
   char                 *modelName; 
   char                 *modelNumber; 
   char                 *versionStr; 
   char                 *serialNum; 
   char                 *udn;    
   rtv_device_version_t  version;      //Software version info. (built from versionStr)
   int                   autodiscovered;
   __u32                 status;
} rtv_device_info_t;

//+****************************************
// RTV Exported channel structure
//+****************************************
typedef struct rtv_channel_export_t 
{
   __u32  channel_id;                 // Time when channel was created
   char  *channel_type;               // static string (recording_type)
   char  *days_of_week;               // malloc'd string.
   int    tuning;                     // TV channel number
   char   label[50];
} rtv_channel_export_t;

//+****************************************
// Show Flags structure
//+****************************************
typedef struct rtv_show_flags_t 
{
   int guaranteed;
   int guide_id;
   int cc;
   int stereo;
   int repeat;
   int sap;
   int letterbox;
   int movie;
   int multipart;
} rtv_show_flags_t;

//+****************************************
// RTV Exported Show structure
//+****************************************
typedef struct rtv_show_export_t 
{
   __u32               show_id;            //show id: show creation time
   __u32               channel_id;         //channel id: channel creation time
   int                 rtvchan_idx;        //index into rtv channel array (JBH: Not yet parsing RTV channel info)
   int                 unavailable;        //set to 1 if show is unavailable. ie: has been deleted but is still in the guide.
   rtv_show_flags_t    flags;
   rtv_show_quality_t  quality;            // recording quality 
   int                 input_source;       // xref with rtv_xref_input_source()
   int                 tuning;             // TV channel number
   char                tune_chan_name[16]; // Tuned channel name
   char                tune_chan_desc[32]; // Tuned channel description
   char               *title;
   char               *episode; 
   char               *description; 
   char               *actors; 
   char               *guest; 
   char               *suzuki; 
   char               *producer; 
   char               *director; 
   char               *genre;
   char               *rating;             // TV/Movie rating 
   char               *rating_extended;    // TV/Movie extended rating
   int                 movie_stars;        // Zero to 5 stars
   int                 movie_year;         // Year made
   int                 movie_runtime;      // Movie run time (minutes)
   char               *file_name;          // mpg file name
   rtv_fs_file_t      *file_info;          // mpg file info
   __u32               gop_count;          // MPEG Group of Picture Count
   __u32               duration_sec;       // duration of the recording (seconds)
   char               *duration_str;       // duration of the recording ( hours/minutes string)
   __u8                padding_before;     // minutes padding before show
   __u8                padding_after;      // minutes padding after show
   __u32               sch_start_time;     // Scheduled Time of the Show (TimeT format)
   __u32               sch_show_length;    // Scheduled Length of Show (minutes)
   char               *sch_st_tm_str;      // Scheduled Time of the Show (String)
   __u32               status;
} rtv_show_export_t;

//+****************************************
// RTV Exported Guide structure
//+****************************************
typedef struct rtv_guide_export_t 
{
   char                  *timestamp;            //Snapshot timestamp
   unsigned int           num_channels;
   rtv_channel_export_t  *channel_list;
   unsigned int           num_rec_shows;
   rtv_show_export_t     *rec_show_list;
   unsigned long          status;
} rtv_guide_export_t;

//+****************************************
// Top level RTV device structure
//+****************************************
typedef struct rtv_device_t
{
   rtv_device_info_t  device;
   rtv_guide_export_t guide;
} rtv_device_t;

//+****************************************
// List of RTV devices
//+****************************************
typedef struct rtv_device_list_t
{
   int           num_rtvs; // Number of rtv's in list
   rtv_device_t *rtv;      // List of rtv_device_t
} rtv_device_list_t;


//+****************************************
// ndx file formats
//+****************************************

// RTV 4K ndx file header: size=32 bytes
//
typedef struct rtv_ndx_22_header_t 
{
   __u8 major_version;    // 2
   __u8 minor_version;    // 2
   __u8 flags;            // 0x01 = copy protected; none others seen
   __u8 unused[29];       // all 0s
} rtv_ndx_22_header_t;

// RTV 4K ndx record: size=32 bytes
//
typedef struct rtv_ndx_22_record_t 
{
   __u8  flag_1;
   __u8  commercial_flag;
   __u16 video_offset;     // relative to stream_position
   __u8  unk_fe;
   __u8  macrovision;
   __u16 macrovision_count;
   __u32 audio_offset;     // relative to stream_position + video_offset
   __u32 unused1;          // always 0 */
   __u64 timestamp;        // seconds * 10e9, from an unknown base
   __u64 stream_position;
} rtv_ndx_22_record_t; 

// RTV 5K ndx file header: size=32 bytes
//
typedef struct rtv_ndx_30_header_t 
{
   __u8 major_version;    // 3
   __u8 minor_version;    // 0
   __u8 unknown[30];      // unknown
} rtv_ndx_30_header_t;

// RTV 5K ndx record: size=24 bytes
//
typedef struct rtv_ndx_30_record_t 
{
   __u64 timestamp;            // 8 byte timestamp, in nanoseconds
   __u64 filepos_iframe;       // File position to an I-frame
   __u32 iframe_size;          // Size of the I-Frame (including PES Headers)
   __u32 empty;                // Always Zero, possibly for alignment
} rtv_ndx_30_record_t; 


//+****************************************
// evt file processing
//+****************************************

// RTV 5K evt record: size=24 bytes
//
#define RTV_EVT_HDR_SZ (8)
typedef struct rtv_evt_record_t 
{
   __u64 timestamp;            // 8 byte timestamp, in nanoseconds
   __u32 unknown1;
   __u32 data_type;            // 1=audio 2=video
   __u32 audiopower;
   __u32 blacklevel;
} rtv_evt_record_t; 

//
//
typedef struct rtv_chapter_mark_parms_t
{
   int   p_seg_min; //minimum program segment time. (sec)
   int   scene_min; //minimum scene segment time (sec)
   int   buf_sz;    //evt file buffer size
   char *buf;       //evt file buffer 
} rtv_chapter_mark_parms_t;

//
//
typedef struct rtv_prog_seg_t {
   __u64 start;
   __u64 stop;
} rtv_prog_seg_t;

//
//
typedef struct rtv_chapters_t
{
   int              num_chapters;
   rtv_prog_seg_t  *chapter;      //array of chapters
} rtv_chapters_t;


//+************************************************************
// rtv_read_file_chunked callback fxn prototype
// parms:
//         buf:    Read data from the replay device
//         len:    length of data
//         offset: offset into buff for start of data
//         vd:     callback_data pointer passed into rtv_read_file 
// returncode:
//         Return 0 to keep receiving data.
//         Return 1 to to abort the transfer.
//
// NOTE: buf is malloc'd. users callback function must free(buf)
//
//+************************************************************
typedef int (*rtv_read_file_chunked_cb_t)(unsigned char *buf, size_t len, size_t offset, void *vd);

// rtv_read_file() data parameter
//
typedef struct rtv_http_resp_data_t 
{
   char         *buf;        // returned data buffer
   char         *data_start; // start of data in buf
   unsigned int  len;        // length of data in buf
} rtv_http_resp_data_t;

//+************************************************************************
//+******************** Local IP Address & Hostname ***********************
//+************************************************************************
extern char local_ip_address[];
extern char local_hostname[];

//+************************************************************************
//+******************** Exported RTV Device List **************************
//+************************************************************************
extern rtv_device_list_t rtv_devices;

//+************************************************************************
//+**************************   RTV API's *********************************
//+************************************************************************
extern int           rtv_init_lib(void);
extern void          rtv_set_dbgmask(__u32 mask);
extern __u32         rtv_get_dbgmask(void);
extern void          rtv_set_32k_chunks_to_merge(int chunks);
extern int           rtv_route_logs(char *filename);
extern rtv_device_t *rtv_get_device_struct(const char *ipaddr, int *new);
extern int           rtv_free_devices(void);
extern void          rtv_print_device_list(void); 
extern char         *rtv_format_time64_1(__u64 ttk);             // Returned string is malloc'd: user must free
extern char         *rtv_format_time64_2(__u64 ttk);             // Returned string is malloc'd: user must free
extern char         *rtv_format_time32(__u32 t);                 // Returned string is malloc'd: user must free
extern char         *rtv_sec_to_hr_mn_str(unsigned int seconds); // Returned string is malloc'd: user must free
extern char         *rtv_format_nsec64(__u64 nsec);              // Returned string is malloc'd: user must free
extern int           rtv_crypt_test(void);
extern void          rtv_convert_22_ndx_rec(rtv_ndx_22_record_t *rec);
extern void          rtv_convert_30_ndx_rec(rtv_ndx_30_record_t *rec);
extern void          rtv_print_30_ndx_rec(char *tag, int rec_no, rtv_ndx_30_record_t *rec);
extern void          rtv_convert_evt_rec(rtv_evt_record_t *rec);
extern void          rtv_hex_dump(char * tag, unsigned char * buf, unsigned int sz);

extern int  rtv_discover(unsigned int timeout_ms, rtv_device_list_t **device_list);
extern int  rtv_get_device_info(const char *address,  char *queryStr, rtv_device_t **device_p);
extern void rtv_free_device_info(rtv_device_info_t  *devinfo_p); 
extern void rtv_print_device_info(const rtv_device_info_t *devinfo); 

extern int  rtv_get_guide_snapshot(const rtv_device_info_t  *device,
                                   const char               *cur_timestamp,
                                         rtv_guide_export_t *guide);

extern void rtv_print_show(const rtv_show_export_t *show, int num);
extern void rtv_print_guide(const rtv_guide_export_t *guide); 
extern void rtv_free_guide(rtv_guide_export_t *guide); 

extern int rtv_is_show_inuse(const rtv_device_info_t   *device,
                             const rtv_guide_export_t  *guide, 
                             const unsigned int         show_idx,
                             int                       *in_use);

extern int rtv_delete_show(const rtv_device_info_t   *device,
                           const rtv_guide_export_t  *guide, 
                           unsigned int               show_idx,
                           __u32                      show_id);

extern int rtv_release_show_and_wait(const rtv_device_info_t *device,
                                     rtv_guide_export_t      *guide, 
                                     __u32                    show_id);

extern int rtv_get_play_position(const rtv_device_info_t   *device,
                                 const rtv_guide_export_t  *guide, 
                                 const unsigned int         show_idx,
                                 unsigned int              *play_pos);
   
extern char *rtv_xref_quality(int key);
extern char *rtv_xref_input_source(int key);

extern int rtv_parse_evt_file( rtv_chapter_mark_parms_t evtfile_parms, rtv_chapters_t *chapter_struct);

extern int  rtv_get_volinfo( const rtv_device_info_t  *device, const char *name, rtv_fs_volume_t **volinfo );
extern void rtv_free_volinfo(rtv_fs_volume_t **volinfo); 
extern void rtv_print_volinfo(const rtv_fs_volume_t *volinfo); 

extern int  rtv_get_file_info( const rtv_device_info_t  *device, const char *name,  rtv_fs_file_t *fileinfo  );
extern void rtv_free_file_info(rtv_fs_file_t *fileinfo); 
extern void rtv_print_file_info(const rtv_fs_file_t *fileinfo); 

extern int  rtv_get_filelist( const rtv_device_info_t  *device, const char *name, int details, rtv_fs_filelist_t **filelist );
extern void rtv_free_file_list( rtv_fs_filelist_t **filelist ); 
extern void rtv_print_file_list(const rtv_fs_filelist_t *filelist, int detailed);

extern __u32  rtv_read_file_chunked( const rtv_device_info_t    *device, 
                                     const char                 *filename, 
                                     __u64                       pos,        //fileposition
                                     __u64                       size,       //amount of file to read ( 0 reads all of file )
                                     unsigned int                ms_delay,   //mS delay between reads
                                     rtv_read_file_chunked_cb_t  callback_fxn,
                                     void                       *callback_data ); 


extern __u32  rtv_read_file( const rtv_device_info_t  *device, 
                             const char               *filename, 
                             __u64                     pos,        //fileposition
                             __u64                     size,       //amount of file to read
                             rtv_http_resp_data_t     *data  );    //returned file data

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* __RTVLIB_H__ */
