#ifndef WMAP_H
#define WMAP_H

// Flags for wmap
#define MAP_PRIVATE 0x0001
#define MAP_SHARED 0x0002
#define MAP_POPULATE 0x0004
#define MAP_FIXED 0x0008

// for `getpgdirinfo`
#define MAX_UPAGE_INFO 32
struct pgdirinfo {
    uint n_upages;
    uint va[MAX_UPAGE_INFO];
    uint pa[MAX_UPAGE_INFO];
};

// for `getwmapinfo`
#define MAX_WMMAP_INFO 16
struct wmapinfo {
    int total_mmaps;
    int addr[MAX_WMMAP_INFO];
    int length[MAX_WMMAP_INFO];
    int n_loaded_pages[MAX_WMMAP_INFO];
};

#endif // WMAP_H
