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

//+**********************************************************
// Define architecture specific data types
//+**********************************************************
typedef unsigned char      __u8;
typedef unsigned short     __u16;
typedef unsigned long      __u32;
typedef unsigned long long __u64;

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
// Top level RTV structure
//+****************************************
typedef struct rtv_device_t
{
   rtv_device_info_t  device;
   rtv_guide_export_t guide;
} rtv_device_t;


//+************************************************************************
//+**************************   RTV API's *********************************
//+************************************************************************
extern void   rtv_set_dbgmask(__u32 mask);
extern __u32  rtv_get_dbgmask(void);
extern int    rtv_crypt_test(void);
extern int    rtv_httpfs_cli_cmd(const rtv_device_info_t *devinfo, int argc, char **argv);

extern int  rtv_get_device_info(const char *address, rtv_device_info_t *devinfo);
extern void rtv_free_device_info(rtv_device_info_t  *devinfo_p); 
extern void rtv_print_device_info(const rtv_device_info_t *devinfo); 

extern int  rtv_get_guide_snapshot(const rtv_device_info_t  *device,
                                   const char               *cur_timestamp,
                                         rtv_guide_export_t *guide          );
extern void rtv_free_show(rtv_show_export_t *show); 
extern void rtv_print_show(const rtv_show_export_t *show, int num);
extern void rtv_print_guide(const rtv_guide_export_t *guide); 
extern void rtv_free_guide(rtv_guide_export_t *guide); 

// TODO: Following 2 api's need to be abstracted before being exported.
//
extern unsigned long hfs_do_simple(char **presult, const rtv_device_info_t *device, const char * command, ...);
extern unsigned long hfs_do_chunked(void (*fn)(unsigned char *, size_t, void *),
                                    void *v,
                                    const rtv_device_info_t *device,
                                    __u16 msec_delay,
                                    const char *command,
                                    ...);


#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* __RTVLIB_H__ */
