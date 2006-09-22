#ifndef WEB_CONFIG_H
#define WEB_CONFIG_H

struct shared_disk_t {
    char ip[64];
    char local_dir[64];
    char remote_dir[64];
    char is_nfs;
};

typedef struct {
    char tz[30];
    char time_server[64];
    char cwd[64];
    char vnc_server[64];
    int  vnc_port;
    int  startup_this_feature;
    int  bitmask;
    char mythtv_ringbuf[64];
    char screen_capture_file[64];
    char mythtv_recdir[64];
    char rtv_init_str[64];
    char font[64];
    char imagedir[64];
    int pick_playlist;
    char playlist[64];
    char live365_userid[33];
    char live365_password[33];
    char share_user[16];
    char share_password[16];
    struct shared_disk_t share_disk[3];
    char vlc_server[64];
    char mvp_server[64];
    int rfb_mode;
    int flicker;
    int control;
    char wol_mac[18];
} web_config_t;

extern web_config_t *web_config;

extern int web_port;

extern int web_server;

extern void reset_web_config(void);
extern void load_web_config(char *font);

#endif /* WEB_CONFIG_H */
