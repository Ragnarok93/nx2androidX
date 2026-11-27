#ifndef DM_OVERLAY_SETTINGS_SCROLL_H
#define DM_OVERLAY_SETTINGS_SCROLL_H
// Included after the shared menu item table. Logical row ids remain stable;
// the viewport only contains enabled settings. Scroll units are 1/64 of a row.
enum { OV_SETTINGS_UNIT=64, OV_SETTINGS_PENDING=0, OV_SETTINGS_SCROLL=1, OV_SETTINGS_SLIDER=2 };
#define OV_SETTINGS_VIEW_ROWS 6
static inline int ov_settings_count(int page) {
    int n=0;
    for(int row=1;row<OVM_ROWS-1;row++) n+=ov_menu_item(page,row)>=0;
    return n;
}
static inline int ov_settings_max(int page) {
    int n=ov_settings_count(page)-OV_SETTINGS_VIEW_ROWS;
    return n>0?n*OV_SETTINGS_UNIT:0;
}
static inline int ov_settings_clamp(int page,int scroll) {
    int max=ov_settings_max(page);
    return scroll<0?0:scroll>max?max:scroll;
}
static inline int ov_settings_index(int page,int row) {
    if(row<=0||row>=OVM_ROWS-1||ov_menu_item(page,row)<0)return -1;
    int n=0;
    for(int i=1;i<row;i++)n+=ov_menu_item(page,i)>=0;
    return n;
}
static inline int ov_settings_reveal(int page,int scroll,int row) {
    int i=ov_settings_index(page,row);scroll=ov_settings_clamp(page,scroll);
    if(i<0)return scroll;
    int first=i*OV_SETTINGS_UNIT,last=first+OV_SETTINGS_UNIT;
    if(first<scroll)scroll=first;
    if(last>scroll+OV_SETTINGS_VIEW_ROWS*OV_SETTINGS_UNIT)scroll=last-OV_SETTINGS_VIEW_ROWS*OV_SETTINGS_UNIT;
    return ov_settings_clamp(page,scroll);
}
static inline void ov_settings_view(const float p[4],float v[4]) {
    // Fixed-height rows independent of total settings. Header, tabs and Resume
    // occupy reserved space. This also scales proportionately on small screens.
    const float pad=p[3]*0.038f, title=p[3]*0.066f;
    const float rh=(p[3]-2*pad-title)/(OV_SETTINGS_VIEW_ROWS+2);
    v[0]=p[0]+pad;v[1]=p[1]+pad+title+rh;
    v[2]=p[2]-2*pad;v[3]=rh*OV_SETTINGS_VIEW_ROWS;
}
static inline int ov_settings_row(const float p[4],int page,int row,int scroll,float r[4]) {
    float v[4];ov_settings_view(p,v);
    float rh=v[3]/OV_SETTINGS_VIEW_ROWS;
    r[0]=v[0];r[2]=v[2];r[3]=rh;
    if(row==0)r[1]=v[1]-rh;
    else if(row==OVM_ROWS-1)r[1]=v[1]+v[3];
    else {
        int i=ov_settings_index(page,row);if(i<0)return 0;
        r[1]=v[1]+(i-(float)ov_settings_clamp(page,scroll)/OV_SETTINGS_UNIT)*rh;
    }
    return 1;
}
static inline int ov_settings_hit(const float p[4],int page,int scroll,float x,float y) {
    if(x<p[0]||x>=p[0]+p[2]||y<p[1]||y>=p[1]+p[3])return -2;
    float v[4],r[4];ov_settings_view(p,v);
    if(x<v[0]||x>=v[0]+v[2])return -1;
    if(y>=v[1]&&y<v[1]+v[3]) {
        for(int row=1;row<OVM_ROWS-1;row++)if(ov_settings_row(p,page,row,scroll,r)&&y>=r[1]&&y<r[1]+r[3])return row;
        return -1;
    }
    ov_settings_row(p,page,0,scroll,r);if(y>=r[1]&&y<v[1])return 0;
    ov_settings_row(p,page,OVM_ROWS-1,scroll,r);if(y>=r[1]&&y<r[1]+r[3])return OVM_ROWS-1;
    return -1;
}
static inline int ov_settings_slider(int item) {
    return item==OVM_OPACITY||item==OVM_FSR_SHARP||item==OVM_DRS_MIN||item==OVM_RS_MAX;
}
static inline int ov_settings_gesture(int slider,int current,float dx,float dy,float rh) {
    if(current!=OV_SETTINGS_PENDING)return current;
    if(fmaxf(fabsf(dx),fabsf(dy))<rh*0.14f)return OV_SETTINGS_PENDING;
    return fabsf(dy)>=fabsf(dx)?OV_SETTINGS_SCROLL:slider?OV_SETTINGS_SLIDER:OV_SETTINGS_PENDING;
}
static inline float ov_settings_fraction(float x,const float p[4]) {
    float v[4];ov_settings_view(p,v);
    float f=(x-v[0]-v[2]*0.40f)/(v[2]*0.44f);
    return f<0?0:f>1?1:f;
}
#endif
