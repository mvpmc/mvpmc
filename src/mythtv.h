
#define PROGRAM_ADJUST  3600

struct program {
        int chanid;
        char title[130];
        char subtitle[130];
        char description[256];
        char starttime[25];
        char endtime[25];
        char programid[20];
        char seriesid[12];
        char category[64];
        int recording;
	char rec_status[2];
	int channum;
};

struct channel {
        int chanid;
        int channum;
        char callsign[20];
        char name[64];
};


