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
typedef unsigned char      __u8;
typedef unsigned short     __u16;
typedef unsigned long      __u32;
typedef unsigned long long __u64;
typedef signed   long long __s64;

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
   __u32                 status;
} rtv_device_info_t;

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
   rtv_show_flags_t  flags;
   char             *title;
   char             *episode; 
   char             *description; 
   char             *actors; 
   char             *guest; 
   char             *suzuki; 
   char             *producer; 
   char             *director; 
   char             *file_name;
//   char             *ch_id; 
//   char             *ch_name;
//   char             *ch_num; 
//   char             *quality; 
//   char             *duration; 
//   char             *rec_time; 
//   char             *rec_date; 
//   char             *id;
   __u32               status;
} rtv_show_export_t;

//+****************************************
// RTV Exported Guide structure
//+****************************************
typedef struct rtv_guide_export_t 
{
   char              *timestamp;            //Snapshot timestamp
   unsigned int       num_rec_shows;
   rtv_show_export_t *rec_show_list;
   unsigned long      status;
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
   char              *time_str;
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

//+****************************************************
// rtv_read_file parameter that specifies how many 32K
// byte chunks to merge together before passing data 
// to application
//+****************************************************
typedef enum rtv_mergechunks_t 
{
   RTV_MERGECHUNKS_0 = 0,
   RTV_MERGECHUNKS_2 = 2,
   RTV_MERGECHUNKS_4 = 4
} rtv_mergechunks_t;

//+************************************************************
// rtv_read_file callback fxn prototype
// parms:
//         buf: Read data from the replay device
//         len: buf size
//         vd: callback_data pointer passed into rtv_read_file 
// returncode:
//         Return 0 to keep receiving data.
//         Return 1 to to abort the transfer.
//+************************************************************
typedef int (*rtv_read_chunked_cb_t)(unsigned char *buf, size_t len, void *vd);



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
extern rtv_device_t *rtv_get_device_struct(const char *ipaddr, int *new);
extern int           rtv_free_devices(void);
extern void          rtv_print_device_list(void); 
extern int           rtv_route_logs(char *filename);
extern char         *rtv_format_time(__u64 ttk); 
extern void          rtv_set_dbgmask(__u32 mask);
extern __u32         rtv_get_dbgmask(void);
extern int           rtv_crypt_test(void);

extern int rtv_discover(unsigned int timeout_ms, rtv_device_list_t **device_list);
extern int  rtv_get_device_info(const char *address,  char *queryStr, rtv_device_t **device_p);
extern void rtv_free_device_info(rtv_device_info_t  *devinfo_p); 
extern void rtv_print_device_info(const rtv_device_info_t *devinfo); 

extern int  rtv_get_guide_snapshot(const rtv_device_info_t  *device,
                                   const char               *cur_timestamp,
                                         rtv_guide_export_t *guide          );
extern void rtv_free_show(rtv_show_export_t *show); 
extern void rtv_print_show(const rtv_show_export_t *show, int num);
extern void rtv_print_guide(const rtv_guide_export_t *guide); 
extern void rtv_free_guide(rtv_guide_export_t *guide); 

extern int  rtv_get_volinfo( const rtv_device_info_t  *device, const char *name, rtv_fs_volume_t **volinfo );
extern void rtv_free_volinfo(rtv_fs_volume_t **volinfo); 
extern void rtv_print_volinfo(const rtv_fs_volume_t *volinfo); 

extern int  rtv_get_file_info( const rtv_device_info_t  *device, const char *name,  rtv_fs_file_t *fileinfo  );
extern void rtv_free_file_info(rtv_fs_file_t *fileinfo); 
extern void rtv_print_file_info(const rtv_fs_file_t *fileinfo); 

extern int  rtv_get_filelist( const rtv_device_info_t  *device, const char *name, int details, rtv_fs_filelist_t **filelist );
extern void rtv_free_file_list( rtv_fs_filelist_t **filelist ); 
extern void rtv_print_file_list(const rtv_fs_filelist_t *filelist, int detailed);

extern __u32  rtv_read_file( const rtv_device_info_t *device, 
                             const char              *filename, 
                             __u64                    pos,        //fileposition
                             __u64                    size,       //amount of file to read ( 0 reads all of file )
                             unsigned int             ms_delay,   //mS delay between reads
                             rtv_read_chunked_cb_t    callback_fxn,
                             void                    *callback_data                                     ); 

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* __RTVLIB_H__ */
