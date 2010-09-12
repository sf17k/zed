#include <SDL/SDL.h>
#include <SDL/SDL_opengl.h>
#include <iostream>
#include <math.h>
#include "MersenneTwister.h"
//#include "SOIL.h"
#include "game.h"
#include "net.h"

using namespace std;

namespace Game{

    const bool FULLSCREEN=false;
    const int WINDOW_W=800;
    const int WINDOW_H=600;
    const float FOV=105.0f;
    const int CENTER_X=WINDOW_W/2;
    const int CENTER_Y=WINDOW_H/2;

    bool isserver=false;
    bool sdl_started=false;
    int gamestate=0;

    SDL_Surface *screen=NULL;
    bool isKeyPressed[SDLK_LAST];
    bool isKeyDown[SDLK_LAST];

    struct vect{
        float x,y,z;
        inline vect():x(0.0f),y(0.0f),z(0.0f){}
        inline vect(const float _x, const float _y, const float _z):x(_x),y(_y),z(_z){}
        inline vect& set(const vect& v){ x=v.x; y=v.y; z=v.z; return *this; }
        inline vect& set(const float _x,const float _y,const float _z){x=_x;y=_y;z=_z;return *this;}
        inline vect& add(const vect& v){ x+=v.x; y+=v.y; z+=v.z; return *this; }
        inline vect& sub(const vect& v){ x-=v.x; y-=v.y; z-=v.z; return *this; }
        inline vect& add(const float _x,const float _y,const float _z){x+=_x;y+=_y;z+=_z;return *this;}
        inline vect& adds(const vect& v, const float s){ x+=v.x*s; y+=v.y*s; z+=v.z*s; return *this; }
        inline vect& cross(const vect& v){
            const float ox=x,oy=y; x=oy*v.z-z*v.y; y=z*v.x-ox*v.z; z=ox*v.y-oy*v.x; return *this; }
        inline vect& normalize(){
            float d=x*x+y*y+z*z; if(d>0){ d=1.0f/sqrtf(d); x*=d; y*=d; z*=d; } return *this; }
    };
    inline float dot(const vect& v, const vect& w){ return v.x*w.x+v.y*w.y+v.z*w.z; }
    inline float len(const vect& v){ return sqrtf(v.x*v.x+v.y*v.y+v.z*v.z); }

    struct OBB{
        vect ax,ay,az; //orthonormal basis
        vect p,e; //center position, extents
        inline OBB():ax(1.0f,0.0f,0.0f),ay(0.0f,1.0f,0.0f),az(0.0f,0.0f,1.0f){}
        inline OBB(const vect& _p, const vect& _e)
            :ax(1.0f,0.0f,0.0f),ay(0.0f,1.0f,0.0f),az(0.0f,0.0f,1.0f),p(_p),e(_e){}
        void rotatex(const float r){
            const float co=cosf(r); const float si=sinf(r); vect by,bz;
            by.adds(ay,co).adds(az,si); bz.adds(ay,-si).adds(az,co); ay.set(by); az.set(bz); }
        void rotatey(const float r){
            const float co=cosf(r); const float si=sinf(r); vect bx,bz;
            bx.adds(az,-si).adds(ax,co); bz.adds(az,co).adds(ax,si); ax.set(bx); az.set(bz); }
        void rotatez(const float r){
            const float co=cosf(r); const float si=sinf(r); vect bx,by;
            bx.adds(ax,co).adds(ay,si); by.adds(ax,-si).adds(ay,co); ax.set(bx); ay.set(by); }
        void translate(const vect& v){ p.adds(ax,v.x).adds(ay,v.y).adds(az,v.z); }
        void ltow(vect& v)const{ vect r; r.adds(ax,v.x).adds(ay,v.y).adds(az,v.z).add(p); v.set(r); }
        void wtol(vect& v)const{ v.sub(p); v.set(dot(ax,v),dot(ay,v),dot(az,v)); }
    };

    int frames=0;
    int plid=-1; //local player id
    vect cam;
    vect look;
    float sensitivity=0.0010f;
    char healthhud[400];

    MTRand rng;
    char *map=NULL;
    int *cols=NULL;
    const char INSIDE_BIT=0x01;
    const char DOORX_BIT=0x02;
    const char DOORZ_BIT=0x04;

    const int MAX_PLAYERS=8;
    const int MAX_ZEDS=4096;
    const int MAX_BULLETS=64;
    const int MAX_PARTICLES=1024;
    const unsigned char Z_NONE=0;
    const unsigned char Z_DEAD=1;
    const unsigned char Z_WANDERING=3; //wandering about
    const unsigned char Z_ATTACKING=4; //running at players
    const unsigned char Z_HEALTH=10; //pickup
    const unsigned char Z_AMMO=11; //pickup

    struct Player{
        vect p;
        vect v;
        float lookr,lookp;
        float shootdelay;
        int health,ammo;
        bool onground;
        unsigned char keys;
        unsigned char state;
    }pl[MAX_PLAYERS];

    struct Zed{
        vect p;
        vect v;
        float rot;
        int ix,iz;
        int cnext,cprev;
        unsigned char state;
    }zed[MAX_ZEDS];

    struct Bullet{
        vect p;
        vect v;
    }bullets[MAX_BULLETS];

    struct Particle{
        vect p;
        float age;
    }particles[MAX_PARTICLES];

    enum {
        KB_LEFT=1,
        KB_RIGHT=2,
        KB_FORWARD=4,
        KB_BACK=8,
        KB_JUMP=16,
        KB_USE=32,
        KB_FIRE=64
    };

    void setKeys(int i, unsigned char keys){
        if(pl[i].state)
            pl[i].keys=keys;
    }

    void setAim(int i, unsigned short aimr, unsigned short aimp){
        if(pl[i].state){
            pl[i].lookr=(float)aimr*M_PI*2.0f/65536.0f;
            pl[i].lookp=(float)aimp*M_PI/65536.0f-M_PI*0.5f;
            if(pl[i].lookr>M_PI*2) pl[i].lookr-=M_PI*2;
            if(pl[i].lookr<0) pl[i].lookr+=M_PI*2;
            if(pl[i].lookp>0.49f*M_PI) pl[i].lookp=0.49f*M_PI;
            if(pl[i].lookp<-0.49f*M_PI) pl[i].lookp=-0.49f*M_PI;
        }
    }

    void setClientID(int id){
        plid=id;
        if(plid<0 || plid>=MAX_PLAYERS)
            plid=-1;
        gamestate=1;
        pl[plid].state=1;
    }

    unsigned short getAimr(int i){
        return (unsigned short)(pl[i].lookr*65536.0f/(M_PI*2.0f));
    }

    unsigned short getAimp(int i){
        return (unsigned short)((pl[i].lookp+M_PI*0.5f)*65536.0f/M_PI);
    }

    unsigned char* getMap(){
        return (unsigned char*)map;
    }

    int getClientUpdate(unsigned char *keys, unsigned short *aimr, unsigned short *aimp){
        if(plid==-1)
            return -1;
        *keys=pl[plid].keys;
        *aimr=getAimr(plid);
        *aimp=getAimp(plid);
        return 0;
    }

    void updateHealthHud(){
        if(plid!=-1){
            const float r=(float)pl[plid].health/100.0f;
            for(int i=0;i<400;i++)
                healthhud[i]=rng()<r?1:0;
        }
    }

    int getPlayerUpdate(int i, unsigned short *pv, unsigned char* kha){
        if(i<0 || i>=MAX_PLAYERS || pl[i].state==0)
            return -1;
        pv[0]=(unsigned short)(pl[i].p.x*65536.0f/512.0f);
        pv[1]=(unsigned short)(pl[i].p.y*65536.0f/512.0f);
        pv[2]=(unsigned short)(pl[i].p.z*65536.0f/512.0f);
        pv[3]=(signed short)(pl[i].v.x*4.0f);
        pv[4]=(signed short)(pl[i].v.y*4.0f);
        pv[5]=(signed short)(pl[i].v.z*4.0f);
        pv[6]=getAimr(i);
        pv[7]=getAimp(i);
        kha[0]=pl[i].keys;
        kha[1]=(signed char)pl[i].health;
        kha[2]=(signed char)pl[i].ammo;
        return 0;
    }

    int setPlayerUpdate(int i, const unsigned short *pv, const unsigned char* kha){
        if(i<0 || i>=MAX_PLAYERS)
            return -1;
        pl[i].p.set(
        ((float)pv[0])*512.0f/65536.0f,
        ((float)pv[1])*512.0f/65536.0f,
        ((float)pv[2])*512.0f/65536.0f);
        pl[i].v.set(
        ((float)((signed short)pv[3]))/4.0f,
        ((float)((signed short)pv[4]))/4.0f,
        ((float)((signed short)pv[5]))/4.0f);
        if(i!=plid){
            setAim(i,pv[6],pv[7]);
            pl[i].keys=kha[0];
        }
        pl[i].health=(signed char)kha[1];
        pl[i].ammo=(signed char)kha[2];
        pl[i].state=1;
        if(i==plid)
            updateHealthHud();
        return 0;
    }

    void respawnPlayer(int p){
        pl[p].p.set(248.0f,0.0f,248.0f);
        pl[p].v.set(0.0f,0.0f,0.0f);
        pl[p].lookr=0.0f;
        pl[p].lookp=0.0f;
        pl[p].shootdelay=0.5f;
        pl[p].health=100;
        pl[p].ammo=30;
        pl[p].onground=true;
        pl[p].keys=0;
        pl[p].state=1;
        if(p==plid){
            cam.set(pl[p].p);
            cam.y+=2.5f;
            look.set(0.0f,0.0f,0.0f);
            updateHealthHud();
        }
    }

    void removePlayer(int p){
        pl[p].p.set(0.0f,0.0f,0.0f);
        pl[p].v.set(0.0f,0.0f,0.0f);
        pl[p].keys=0;
        pl[p].state=0;
    }

    int initServer(){
        isserver=true;

        //game vars
        frames=0;

        if(map) delete[] map;
        map=new char[32*32];
        if(cols) delete[] cols;
        cols=new int[32*32];
        //clear
        for(int i=0;i<32*32;i++)
            map[i]=0;
        for(int i=0;i<32*32;i++)
            cols[i]=-1;
        for(int i=0;i<MAX_ZEDS;i++)
            zed[i].state=Z_NONE;
        //buildings
        for(int c=3;c>0;--c){
            for(int iy=1;iy<31;iy++)
            for(int ix=1;ix<31;ix++){
                if(iy!=13 && ix!=13 && rng.randInt(1)){
                    const int i=iy*32+ix;
                    if(map[i-32]&&map[i+32] || map[i-1]&&map[i+1]
                        || map[i-33]&&!map[i-32]&&!map[i-1]
                        || map[i-31]&&!map[i-32]&&!map[i+1]
                        || map[i+31]&&!map[i+32]&&!map[i-1]
                        || map[i+33]&&!map[i+32]&&!map[i+1])
                        continue;
                    map[i]=INSIDE_BIT;
                }
            }
        }
        //doorways
        for(int iy=1;iy<31;iy++)
        for(int ix=1;ix<31;ix++){
            const int i=iy*32+ix;
            if(map[i]&INSIDE_BIT){
                switch(rng.randInt(3)){
                case 0: map[i]|=DOORX_BIT; break;
                case 1: map[i]|=DOORZ_BIT; break;
                case 2: map[i-1]|=DOORX_BIT; break;
                case 3: map[i-32]|=DOORZ_BIT; break;
                }
                if(rng.rand()<0.20)
                    switch(rng.randInt(3)){
                    case 0: map[i]|=DOORX_BIT; break;
                    case 1: map[i]|=DOORZ_BIT; break;
                    case 2: map[i-1]|=DOORX_BIT; break;
                    case 3: map[i-32]|=DOORZ_BIT; break;
                    }
            }
        }
        //ammo+health caches
        int c=0;
        for(int iz=1;iz<31;iz++)
        for(int ix=1;ix<31;ix++)
            if(map[iz*32+ix]&INSIDE_BIT && rng()<0.125f){
                const unsigned char type=rng()<0.25f?Z_HEALTH:Z_AMMO;
                const int s=rng.randInt(2)+rng.randInt(2);
                const float posx=ix*16.0f+8.0f-(float)s*0.5f;
                const float posz=iz*16.0f+8.0f-(float)s*0.5f;
                for(int jz=0;jz<s;jz++)
                for(int jx=0;jx<s;jx++){
                    for(;c<MAX_ZEDS;c++)
                        if(zed[c].state==Z_NONE){
                            zed[c].state=type;
                            zed[c].p.set(posx+jx,0.0f,posz+jz);
                            zed[c].v.set(0.0f,0.0f,0.0f);
                            zed[c].rot=0.0f;
                            zed[c].ix=ix;
                            zed[c].iz=iz;
                            zed[c].cnext=-1;
                            zed[c].cprev=-1;
                            break;
                        }
                }
            }
        //zeds
/*
        for(int iz=1;iz<31;iz++)
        for(int ix=1;ix<31;ix++){
            if(abs(ix-15)+abs(iz-15)<3)
                continue;
            zed[c].state=Z_WANDERING;
            zed[c].p.set(ix*16.0f+8.0f,0.0f,iz*16.0f+8.0f);
            zed[c].v.set(0.0f,0.0f,0.0f);
            zed[c].rot=rng()*M_PI*2;
            zed[c].ix=ix;
            zed[c].iz=iz;
            zed[c].cnext=-1;
            zed[c].cprev=-1;
            cols[iz*32+ix]=c;
            if(c+1<MAX_ZEDS)
                c++;
        }
*/

        for(int i=0;i<MAX_BULLETS;i++)
            bullets[i].p.x=-1;
        for(int i=0;i<MAX_PARTICLES;i++)
            particles[i].p.x=-1;
        for(int i=0;i<MAX_PLAYERS;i++)
            pl[i].state=0;

        return 0;
    }

    int initClient(){
        isserver=false;

        //sdl
        if(!sdl_started){
            sdl_started=true;
            if(SDL_Init(SDL_INIT_VIDEO)!=0){
                cerr<<"unable to initialize SDL: "<<SDL_GetError()<<endl;
                return 1;
            }
            atexit(SDL_Quit);
            SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
            screen=SDL_SetVideoMode(WINDOW_W,WINDOW_H,32,SDL_OPENGL|(FULLSCREEN?SDL_FULLSCREEN:0));
            if(screen==NULL){
                cerr<<"unable to set video mode: "<<SDL_GetError()<<endl;
                return 1;
            }
            SDL_WM_SetCaption("zed","zed");
            SDL_ShowCursor(SDL_DISABLE);
        }

        for(int i=0;i<SDLK_LAST;i++) isKeyPressed[i]=false;
        for(int i=0;i<SDLK_LAST;i++) isKeyDown[i]=false;

        //opengl
	glShadeModel(GL_FLAT);
	glClearDepth(1.0f);
	glFrontFace(GL_CCW);
	glCullFace(GL_BACK);
        glLineWidth(2);
        glPointSize(2);

	const float fogColor[]={0.0f,0.0f,0.0f,1.0f};
	const float fogDensity[]={0.02f};
	glFogfv(GL_FOG_COLOR,fogColor);
	glFogfv(GL_FOG_DENSITY,fogDensity);
	glEnable(GL_FOG);

	glClearColor(0.0f,0.0f,0.0f,0.0f);
        glViewport(0,0,WINDOW_W,WINDOW_H);

        //game vars
        frames=0;
        plid=-1;

        if(map) delete[] map;
        map=new char[32*32];
        if(cols) delete[] cols;
        cols=new int[32*32];
        //clear
        for(int i=0;i<32*32;i++)
            map[i]=0;
        for(int i=0;i<32*32;i++)
            cols[i]=-1;
        for(int i=0;i<MAX_ZEDS;i++)
            zed[i].state=Z_NONE;

        for(int i=0;i<MAX_BULLETS;i++)
            bullets[i].p.x=-1;
        for(int i=0;i<MAX_PARTICLES;i++)
            particles[i].p.x=-1;
        for(int i=0;i<MAX_PLAYERS;i++)
            pl[i].state=0;

        return 0;
    }

    bool keyPressed(SDLKey k){
        return isKeyPressed[k];
    }

    bool keyDown(SDLKey k){
        return isKeyDown[k];
    }

    inline float sqr(float x){ return x*x; }

    void updateColInfo(const int i){
        const int ix=zed[i].p.x/16.0f;
        const int iz=zed[i].p.z/16.0f;
        if(zed[i].ix!=ix || zed[i].iz!=iz){
            if(zed[i].cnext!=-1)
                zed[zed[i].cnext].cprev=zed[i].cprev;
            if(zed[i].cprev!=-1)
                zed[zed[i].cprev].cnext=zed[i].cnext;
            else
                cols[zed[i].iz*32+zed[i].ix]=zed[i].cnext;
            zed[i].cprev=-1;
            zed[i].cnext=cols[iz*32+ix];
            zed[zed[i].cnext].cprev=i;
            cols[iz*32+ix]=i;
            zed[i].ix=ix;
            zed[i].iz=iz;
        }
    }

    void makeZedOBB(OBB* b, const int c){
        const float ZEDW2=0.565685425f;
        b->p.set(zed[c].p);
        if(zed[c].state==Z_DEAD){
            b->p.y+=ZEDW2;
            b->e.set(vect(1.4f,ZEDW2,ZEDW2));
            b->rotatey(-zed[c].rot);
        }else{
            b->p.y+=1.4f;
            b->e.set(vect(ZEDW2,1.4f,ZEDW2));
            b->rotatey(-zed[c].rot-M_PI/4);
        }
    }

    bool intersectionOBB(const OBB& a, const OBB& b){
        vect t(b.p);
        a.wtol(t);
        //rotation matrix
        vect rx(dot(a.ax,b.ax),dot(a.ax,b.ay),dot(a.ax,b.az));
        vect ry(dot(a.ay,b.ax),dot(a.ay,b.ay),dot(a.ay,b.az));
        vect rz(dot(a.az,b.ax),dot(a.az,b.ay),dot(a.az,b.az));
        //a's basis vectors
        if(fabs(t.x) > a.e.x+b.e.x*fabs(rx.x)+b.e.y*fabs(rx.y)+b.e.z*fabs(rx.z)) return false;
        if(fabs(t.y) > a.e.y+b.e.x*fabs(ry.x)+b.e.y*fabs(ry.y)+b.e.z*fabs(ry.z)) return false;
        if(fabs(t.z) > a.e.z+b.e.x*fabs(rz.x)+b.e.y*fabs(rz.y)+b.e.z*fabs(rz.z)) return false;
        //b's basis vectors
        if(fabs(t.x*rx.x+t.y*ry.x+t.z*rz.x) > b.e.x
        + a.e.x*fabs(rx.x)+a.e.y*fabs(ry.x)+a.e.z*fabs(rz.x)) return false;
        if(fabs(t.x*rx.y+t.y*ry.y+t.z*rz.y) > b.e.y
        + a.e.x*fabs(rx.y)+a.e.y*fabs(ry.y)+a.e.z*fabs(rz.y)) return false;
        if(fabs(t.x*rx.z+t.y*ry.z+t.z*rz.z) > b.e.z
        + a.e.x*fabs(rx.z)+a.e.y*fabs(ry.z)+a.e.z*fabs(rz.z)) return false;
        //9 cross products
        if(fabs(t.z*ry.x-t.y*rz.x) > a.e.y*fabs(rz.x)+a.e.z*fabs(ry.x)
        + b.e.y*fabs(rx.z)+b.e.z*fabs(rx.y)) return false;
        if(fabs(t.z*ry.y-t.y*rz.y) > a.e.y*fabs(rz.y)+a.e.z*fabs(ry.y)
        + b.e.x*fabs(rx.z)+b.e.z*fabs(rx.x)) return false;
        if(fabs(t.z*ry.z-t.y*rz.z) > a.e.y*fabs(rz.z)+a.e.z*fabs(ry.z)
        + b.e.x*fabs(rx.y)+b.e.y*fabs(rx.x)) return false;
        if(fabs(t.x*rz.x-t.z*rx.x) > a.e.x*fabs(rz.x)+a.e.z*fabs(rx.x)
        + b.e.y*fabs(ry.z)+b.e.z*fabs(ry.y)) return false;
        if(fabs(t.x*rz.y-t.z*rx.y) > a.e.x*fabs(rz.y)+a.e.z*fabs(rx.y)
        + b.e.x*fabs(ry.z)+b.e.z*fabs(ry.x)) return false;
        if(fabs(t.x*rz.z-t.z*rx.z) > a.e.x*fabs(rz.z)+a.e.z*fabs(rx.z)
        + b.e.x*fabs(ry.y)+b.e.y*fabs(ry.x)) return false;
        if(fabs(t.y*rx.x-t.x*ry.x) > a.e.x*fabs(ry.x)+a.e.y*fabs(rx.x)
        + b.e.y*fabs(rz.z)+b.e.z*fabs(rz.y)) return false;
        if(fabs(t.y*rx.y-t.x*ry.y) > a.e.x*fabs(ry.y)+a.e.y*fabs(rx.y)
        + b.e.x*fabs(rz.z)+b.e.z*fabs(rz.x)) return false;
        if(fabs(t.y*rx.z-t.x*ry.z) > a.e.x*fabs(ry.z)+a.e.y*fabs(rx.z)
        + b.e.x*fabs(rz.y)+b.e.y*fabs(rz.x)) return false;
        return true;
    }

    //returns max of 0-none, 1-wall, 2-side of block, 3-zed
    int collideCharacter(const int me, bool isplayer, float& x, float& y, float& z, const float rad){
        int ret=0;
        if(x<16.0f+rad){ x=16.0f+rad; ret=1; }
        if(z<16.0f+rad){ z=16.0f+rad; ret=1; }
        if(x>31.0f*16.0f-rad){ x=31.0f*16.0f-rad; ret=1; }
        if(z>31.0f*16.0f-rad){ z=31.0f*16.0f-rad; ret=1; }
        const int ix=x/16.0f;
        const int iz=z/16.0f;
        const int i=iz*32+ix;
        const float offx=x-ix*16.0f;
        const float offz=z-iz*16.0f;
        //zeds
        const int jx=offx<8.0f?ix-1:ix+1;
        const int jz=offz<8.0f?iz-1:iz+1;
        const int cs[4]={cols[iz*32+ix],cols[iz*32+jx],cols[jz*32+ix],cols[jz*32+jx]};
        for(int ic=0;ic<4;ic++){
            int c=cs[ic];
            while(c!=-1){
                if(!isplayer && c==me){
                    c=zed[c].cnext;
                    continue;
                }
                if(zed[c].state==Z_DEAD){
                    //upright-obb-upright-cylinder test
                    OBB b;
                    makeZedOBB(&b,c);
                    vect p(x,y,z);
                    b.wtol(p);
                    if(!(p.y<-2.8f || p.y>b.e.y)){
                        bool ontop=p.y>b.e.y-0.3f;
                        bool hit=false;
                        if(fabs(p.x)<b.e.x){
                            if(fabs(p.z)<b.e.z+rad){
                                hit=true;
                                if(!ontop){
                                    p.z=p.z>0?(b.e.z+rad+0.001f):-(b.e.z+rad+0.001f);
                                    b.ltow(p); x=p.x; z=p.z;
                                }
                            }
                        }else if(fabs(p.z)<b.e.z){
                            if(fabs(p.x)<b.e.x+rad){
                                hit=true;
                                if(!ontop){
                                    p.x=p.x>0?(b.e.x+rad+0.001f):-(b.e.x+rad+0.001f);
                                    b.ltow(p); x=p.x; z=p.z;
                                }
                            }
                        }else if(sqr(fabs(p.x)-b.e.x)+sqr(fabs(p.z)-b.e.z)<rad*rad){
                            hit=true;
                            if(!ontop){
                                const float ddx=p.x>0?(p.x-b.e.x):(p.x+b.e.x);
                                const float ddz=p.z>0?(p.z-b.e.z):(p.z+b.e.z);
                                const float dist=sqrtf(ddx*ddx+ddz*ddz);
                                p.x+=(rad-dist+0.001f)*ddx/dist;
                                p.z+=(rad-dist+0.001f)*ddz/dist;
                                b.ltow(p); x=p.x; z=p.z;
                            }
                        }
                        if(hit && ontop){
                            if(isplayer){
                                if(pl[me].v.y<0){
                                    p.y=b.e.y; b.ltow(p);
                                    pl[me].p.y=p.y;
                                    pl[me].v.y=0;
                                }
                                pl[me].onground=true;
                            }else{
                                if(zed[me].v.y<0){
                                    p.y=b.e.y; b.ltow(p);
                                    zed[me].p.y=p.y;
                                    zed[me].v.y=0;
                                }
                            }
                        }else if(hit)
                            if(ret<2) ret=2;
                    }
                }else if(sqr(x-zed[c].p.x)+sqr(z-zed[c].p.z)<2.56f && y+2.8f<zed[c].p.y && zed[c].p.y+2.8f<y){
                    const float dist=sqrtf(sqr(x-zed[c].p.x)+sqr(z-zed[c].p.z));
                    x+=(1.60f-dist)*(x-zed[c].p.x)/dist;
                    z+=(1.60f-dist)*(z-zed[c].p.z)/dist;
                    ret=3;
                }
                c=zed[c].cnext;
            }
        }
        //walls
        const float lastx=x;
        const float lastz=z;
        if(offx<rad){
            if( (map[i]&INSIDE_BIT || map[i-1]&INSIDE_BIT)
                && !(map[i-1]&DOORX_BIT && offz>4+rad && offz<8-rad) )
                x+=rad-offx;
        }else if(offx>16.0f-rad){
            if( (map[i]&INSIDE_BIT || map[i+1]&INSIDE_BIT)
                && !(map[i]&DOORX_BIT && offz>4+rad && offz<8-rad) )
                x+=16.0f-rad-offx;
        }
        if(offz<rad){
            if( (map[i]&INSIDE_BIT || map[i-32]&INSIDE_BIT)
                && !(map[i-32]&DOORZ_BIT && offx>4+rad && offx<8-rad) )
                z+=rad-offz;
        }else if(offz>16.0f-rad){
            if( (map[i]&INSIDE_BIT || map[i+32]&INSIDE_BIT)
                && !(map[i]&DOORZ_BIT && offx>4+rad && offx<8-rad) )
                z+=16.0f-rad-offz;
        }
        if(!isplayer)
            updateColInfo(me);
        if(ret<1 && x!=lastx || z!=lastz)
            ret=1;
        return ret;
    }

    inline float abs(float x){ return x>0?x:-x; }

    int collideLine(float x, float y, float z, float dx, float dy, float dz){
        //>=0 on zed collision, -1 on NO collision, -2 otherwise
        if(x<16.0f || x>31.0f*16.0f || z<16.0f || z>31.0f*16.0f || y<0.0f || y>48.0f)
            return -2;
        const int ix=x/16.0f;
        const int iz=z/16.0f;
        const int ix2=(x+dx)/16.0f;
        const int iz2=(z+dz)/16.0f;
        const float offx=x-ix*16.0f;
        const float offz=z-iz*16.0f;
        const float halfdx=dx*0.5f;
        const float halfdy=dy*0.5f;
        const float halfdz=dz*0.5f;
        if(ix2!=ix || iz2!=iz){
            //across block edge
            if(abs(ix-ix2)+abs(iz-iz2)>1){
                int r=collideLine(x,y,z,halfdx,halfdy,halfdz);
                if(r!=-1)
                    return r;
                return collideLine(x+halfdx,y+halfdy,z+halfdz,halfdx,halfdy,halfdz);
            }
            float p,px,py,pz;
            if(ix2-ix){
                if(!(map[iz*32+ix]&INSIDE_BIT || map[iz*32+ix2]&INSIDE_BIT))
                    goto nowallhit;
                px=max(ix,ix2)*16.0f;
                p=(px-x)/dx;
                pz=z+dz*p;
                py=y+dy*p;
                if(py<6.0f && map[iz*32+min(ix,ix2)]&DOORX_BIT){
                    const float offz=pz-iz*16.0f;
                    if(offz>4.0f && offz<8.0f)
                        goto nowallhit;
                }
            }else{
                if(!(map[iz*32+ix]&INSIDE_BIT || map[iz2*32+ix]&INSIDE_BIT))
                    goto nowallhit;
                pz=max(iz,iz2)*16.0f;
                p=(pz-z)/dz;
                px=x+dx*p;
                py=y+dy*p;
                if(py<6.0f && map[min(iz,iz2)*32+ix]&DOORZ_BIT){
                    const float offx=px-ix*16.0f;
                    if(offx>4.0f && offx<8.0f)
                        goto nowallhit;
                }
            }
            if(py>15.0f) //ceiling
                return -1;
            return -2; //wall hit
        }
        nowallhit:
        const int jx2=offx<8.0f?ix2-1:ix2+1;
        const int jz2=offz<8.0f?iz2-1:iz2+1;
        const int cs[4]={cols[iz2*32+ix2],cols[iz2*32+jx2],cols[jz2*32+ix2],cols[jz2*32+jx2]};
        //const float maxdistsq=sqr(sqrt(halfdx*halfdx+halfdy*halfdy+halfdz*halfdz)+0.80f);
        for(int ic=0;ic<4;ic++){
            int c=cs[ic];
            while(c!=-1){
                //obb-segment test against zed[c]
                OBB b;
                makeZedOBB(&b,c);
                vect p1(x,y,z);
                vect p2(x+halfdx,y+halfdy,z+halfdz);

                b.wtol(p1);
                b.wtol(p2);
                vect l(p2); l.sub(p1); l.normalize();
                vect t(p2);
                p2.sub(p1);
                if(!(fabs(t.x) > b.e.x+fabs(p2.x)
                ||   fabs(t.y) > b.e.y+fabs(p2.y)
                ||   fabs(t.z) > b.e.z+fabs(p2.z)
                || fabs(t.y*l.z-t.z*l.y) > b.e.y*fabs(l.z)+b.e.z*fabs(l.y)
                || fabs(t.x*l.z-t.z*l.x) > b.e.z*fabs(l.x)+b.e.x*fabs(l.z)
                || fabs(t.x*l.y-t.y*l.x) > b.e.x*fabs(l.y)+b.e.y*fabs(l.x)))
                    return c;
                /*if(sqr(x+halfdx-zed[c].p.x)+sqr(z+halfdz-zed[c].p.z)<maxdistsq){
                    float nearx,neary,nearz;
                    if(abs(dx)>abs(dz)){
                        nearx=((zed[c].p.z-z)*dx*dz+x*dz*dz+zed[c].p.x*dx*dx)/(dx*dx+dz*dz);
                        const float p=(nearx-x)/dx;
                        nearz=z+dz*p;
                        neary=y+dy*p;
                    }else{
                        nearz=((zed[c].p.x-x)*dx*dz+z*dx*dx+zed[c].p.z*dz*dz)/(dx*dx+dz*dz);
                        const float p=(nearz-z)/dz;
                        nearx=x+dx*p;
                        neary=y+dy*p;
                    }
                    const float dist=sqrtf(sqr(zed[c].p.x-nearx)+sqr(zed[c].p.z-nearz));
                    //this neary check isn't entirely accurate
                    if(dist<0.80f && neary<2.8f+(0.80f-dist)*abs(dy/sqrtf(dx*dx+dz*dz))){
                        return c;
                    }
                }*/
                c=zed[c].cnext;
            }
        }
        return -1;
    }

    void hitZed(const int i){
        if(zed[i].state==Z_DEAD){
            /*const float RAD=0.80f;
            int count=12;
            for(int j=0;j<MAX_PARTICLES;j++)
                if(particles[j].p.x==-1){
                    particles[j].p.x=zed[i].p.x+rng.rand(RAD*2)-RAD;
                    particles[j].p.y=zed[i].p.y+rng.rand(2.8f);
                    particles[j].p.z=zed[i].p.z+rng.rand(RAD*2)-RAD;
                    particles[j].age=rng.rand(0.20f)+0.20f;
                    if((--count)<=0)
                        break;
                }*/
            if(zed[i].cnext!=-1)
                zed[zed[i].cnext].cprev=zed[i].cprev;
            if(zed[i].cprev!=-1)
                zed[zed[i].cprev].cnext=zed[i].cnext;
            else
                cols[zed[i].iz*32+zed[i].ix]=zed[i].cnext;
            zed[i].cprev=-1;
            zed[i].cnext=-1;
            zed[i].state=Z_NONE;
        }else{
            zed[i].state=Z_DEAD;
        }
    }

    int pollEvents(){
        for(int i=0;i<SDLK_LAST;i++) isKeyPressed[i]=false;
        SDL_Event event;
        while(SDL_PollEvent(&event)){
            switch(event.type){
            case SDL_KEYDOWN:
                isKeyPressed[event.key.keysym.sym]=true;
                isKeyDown[event.key.keysym.sym]=true;
                break;
            case SDL_KEYUP:
                isKeyDown[event.key.keysym.sym]=false;
                break;
            case SDL_MOUSEMOTION: 
                if(plid!=-1){ //TODO: check if grabbed, ungrab on ESC
                    int mx,my;
                    SDL_GetMouseState(&mx,&my);
                    mx-=CENTER_X;
                    my-=CENTER_Y;
                    if(mx!=0 || my!=0){
                        pl[plid].lookr+=mx*sensitivity;
                        if(pl[plid].lookr>M_PI*2) pl[plid].lookr-=M_PI*2;
                        if(pl[plid].lookr<0) pl[plid].lookr+=M_PI*2;
                        pl[plid].lookp-=my*sensitivity;
                        if(pl[plid].lookp>0.49f*M_PI) pl[plid].lookp=0.49f*M_PI;
                        if(pl[plid].lookp<-0.49f*M_PI) pl[plid].lookp=-0.49f*M_PI;
                        SDL_WarpMouse(CENTER_X,CENTER_Y);
                    }
                } break;
            case SDL_QUIT:
                return 1;
            }
        }
        return 0;
    }

    int updateFrame(){
        const float MAX_TIMESTEP=0.1f;
        const float GRAVITY=30.0f;
        const float WALK_SPEED=10.0f;
        const float JUMP_SPEED=10.0f;
        const float CLIMB_SPEED=3.0f;
        const float BULLET_SPEED=70.0f;
        const float SHOOT_DELAY=0.20f;
        const float PARTICLE_AGE=0.10f;
        const float PARTICLE_INTERVAL=1.0f;
        const float PL_RAD=0.80f;
        const float ZED_RANGE=32.0f;
        const int ZED_DAMAGE=30;

        if(!isserver && (pollEvents() || keyPressed(SDLK_F10)))
            return -1;
        if(!isserver && gamestate==0)
            return 0;

        const Uint32 time=SDL_GetTicks();
        static Uint32 timeLast=time;
        const float t=min((float)(time-timeLast)/1000.0f,MAX_TIMESTEP);
        timeLast=time;
        if(t<=0)
            return 0;

        if(!isserver && plid>=0){
            unsigned char mb=SDL_GetMouseState(NULL,NULL);
            bool K_LEFT=keyDown(SDLK_a);
            bool K_RIGHT=keyDown(SDLK_d);
            bool K_FORWARD=keyDown(SDLK_w);
            bool K_BACK=keyDown(SDLK_s);
            bool K_JUMP=keyDown(SDLK_SPACE);
            bool K_USE=keyPressed(SDLK_e);
            bool K_FIRE=mb&SDL_BUTTON(1);
            pl[plid].keys=0;
            pl[plid].keys|=K_LEFT    ? KB_LEFT    :0;
            pl[plid].keys|=K_RIGHT   ? KB_RIGHT   :0;
            pl[plid].keys|=K_FORWARD ? KB_FORWARD :0;
            pl[plid].keys|=K_BACK    ? KB_BACK    :0;
            pl[plid].keys|=K_JUMP    ? KB_JUMP    :0;
            pl[plid].keys|=K_USE     ? KB_USE     :0;
            pl[plid].keys|=K_FIRE    ? KB_FIRE    :0;
        }

        for(int p=0;p<MAX_PLAYERS;p++) if(pl[p].state){
            //player movement
            float dirx=0;
            float diry=0;
            if(pl[p].keys&KB_FORWARD) diry+=1.0f;
            if(pl[p].keys&KB_LEFT) dirx-=1.0f;
            if(pl[p].keys&KB_BACK) diry-=1.0f;
            if(pl[p].keys&KB_RIGHT) dirx+=1.0f;
            if(dirx!=0 && diry!=0){
                dirx*=0.70710678;
                diry*=0.70710678;
            }
            const float lasty=pl[p].p.y;
            pl[p].v.x=WALK_SPEED*(diry*cosf(pl[p].lookr)-dirx*sinf(pl[p].lookr));
            pl[p].v.z=WALK_SPEED*(dirx*cosf(pl[p].lookr)+diry*sinf(pl[p].lookr));
            pl[p].p.adds(pl[p].v,t);
            if(pl[p].p.y>0){
                pl[p].v.y-=GRAVITY*t;
            }else{
                pl[p].p.y=0;
                pl[p].v.y=0;
                pl[p].onground=true;
            }
            if(collideCharacter(p,true,pl[p].p.x,pl[p].p.y,pl[p].p.z,PL_RAD)==2){
                if(pl[p].v.y<CLIMB_SPEED)
                    pl[p].v.y=CLIMB_SPEED;
                pl[p].onground=true;
            }
            if(pl[p].keys&KB_JUMP && pl[p].onground){
                pl[p].v.y=JUMP_SPEED;
                pl[p].p.y+=0.01f;
                pl[p].onground=false;
            }
            if(pl[p].p.y<lasty)
                pl[p].onground=false;
            const vect aim(cosf(pl[p].lookr)*cosf(pl[p].lookp),
                            sinf(pl[p].lookp),
                            sinf(pl[p].lookr)*cosf(pl[p].lookp));

            if(p==plid){
                cam.set(pl[p].p);
                cam.y+=2.5f;
                look.set(cam).add(aim);
            }

            //pickup
            if(pl[p].keys&KB_USE && (pl[p].health<100 || pl[p].ammo<120))
                for(int i=0;i<MAX_ZEDS;i++)
                    if((zed[i].state==Z_HEALTH || zed[i].state==Z_AMMO)
                        && sqr(pl[p].p.x-zed[i].p.x)+sqr(pl[p].p.z-zed[i].p.z)<PL_RAD*PL_RAD*4){
                        if(zed[i].state==Z_HEALTH){
                            if(pl[p].health<100){
                                pl[p].health+=25;
                                if(pl[p].health>100)
                                    pl[p].health=100;
                                if(p==plid)
                                    updateHealthHud();
                                zed[i].state=Z_NONE;
                                break;
                            }
                        }else{
                            if(pl[p].ammo<120){
                                pl[p].ammo+=30;
                                if(pl[p].ammo>120)
                                    pl[p].ammo=120;
                                zed[i].state=Z_NONE;
                                break;
                            }
                        }
                    }

            //shooting
            if(pl[p].shootdelay>0)
                pl[p].shootdelay-=t;
            if(pl[p].ammo>0 && pl[p].keys&KB_FIRE && pl[p].shootdelay<=0){
                for(int i=0;i<MAX_BULLETS;i++)
                    if(bullets[i].p.x==-1){
                        bullets[i].p.set(pl[p].p).adds(aim,0.75f);
                        bullets[i].p.y+=2.5f-0.3f;
                        bullets[i].v.set(pl[p].v).adds(aim,BULLET_SPEED);
                        bullets[i].v.y+=0.15f*GRAVITY;
                        pl[p].ammo--;
                        break;
                    }
                pl[p].shootdelay=SHOOT_DELAY;
            }
        }

        //bullets
        for(int i=0;i<MAX_BULLETS;i++)
            if(bullets[i].p.x!=-1){
                const float dx=bullets[i].v.x*t;
                const float dy=bullets[i].v.y*t;
                const float dz=bullets[i].v.z*t;
                int c;
                if((c=collideLine(bullets[i].p.x,bullets[i].p.y,bullets[i].p.z,dx,dy,dz))!=-1){
                    if(c>=0){ //hit zed
                        hitZed(c);
                    }
                    for(int j=0;j<MAX_PARTICLES;j++)
                        if(particles[j].p.x==-1){
                            particles[j].p.set(bullets[i].p);
                            particles[j].age=PARTICLE_AGE*4.0f;
                            break;
                        }
                    bullets[i].p.x=-1;
                    continue;
                }
                bullets[i].v.y-=GRAVITY*t;
                float dist=sqrtf(dx*dx+dy*dy+dz*dz);
                for(int j=0;j<MAX_PARTICLES;j++)
                    if(particles[j].p.x==-1){
                        particles[j].p.set(bullets[i].p).add(dx*dist,dy*dist,dz*dist);
                        particles[j].age=PARTICLE_AGE;
                        if((dist-=PARTICLE_INTERVAL)<=0)
                            break;
                    }
                bullets[i].p.add(dx,dy,dz);
            }

        //particles
        for(int i=0;i<MAX_PARTICLES;i++)
            if(particles[i].p.x!=-1){
                particles[i].age-=t;
                if(particles[i].age<0)
                    particles[i].p.x=-1;
            }

        //zeds
        for(int i=0;i<MAX_ZEDS;i++) if(zed[i].state!=Z_NONE){
            switch(zed[i].state){
                case Z_DEAD: break;
                case Z_WANDERING: {
                    //wander aimlessly
                    zed[i].p.x+=WALK_SPEED*1.5f*cosf(zed[i].rot)*t;
                    zed[i].p.z+=WALK_SPEED*1.5f*sinf(zed[i].rot)*t;
                    switch(collideCharacter(i,false,zed[i].p.x,zed[i].p.y,zed[i].p.z,PL_RAD)){
                    case 0:
                        for(int p=0;p<MAX_PLAYERS;p++) if(pl[p].state)
                            if(sqr(pl[p].p.x-zed[i].p.x)+sqr(pl[p].p.z-zed[i].p.z)<2.56f){
                                const float dist=sqrtf(sqr(zed[i].p.x-pl[p].p.x)+sqr(zed[i].p.z-pl[p].p.z));
                                zed[i].p.x+=(1.60f-dist)*(zed[i].p.x-pl[p].p.x)/dist;
                                zed[i].p.z+=(1.60f-dist)*(zed[i].p.z-pl[p].p.z)/dist;
                                zed[i].rot=rng.rand(M_PI*2);
                            }
                        break;
                    case 2:
                        if(zed[i].v.y<CLIMB_SPEED)
                            zed[i].v.y=CLIMB_SPEED;
                        break;
                    default:
                        zed[i].rot=rng.rand(M_PI*2);
                        for(int p=0;p<MAX_PLAYERS;p++) if(pl[p].state)
                            if(sqr(pl[p].p.x-zed[i].p.x)+sqr(pl[p].p.z-zed[i].p.z)<ZED_RANGE*ZED_RANGE){
                                zed[i].state=Z_ATTACKING;
                                zed[i].rot=atan2f(pl[p].p.z-zed[i].p.z,pl[p].p.x-zed[i].p.x);
                            }
                    }
                    if(zed[i].p.y>=0){
                        zed[i].p.y+=zed[i].v.y*t;
                        zed[i].v.y-=GRAVITY*t;
                    }else{
                        zed[i].p.y=0;
                        zed[i].v.y=0;
                    }
                    } break;
                case Z_ATTACKING: {
                    zed[i].p.x+=WALK_SPEED*1.5f*cosf(zed[i].rot)*t;
                    zed[i].p.z+=WALK_SPEED*1.5f*sinf(zed[i].rot)*t;
                    for(int p=0;p<MAX_PLAYERS;p++) if(pl[p].state)
                        if(sqr(pl[p].p.x-zed[i].p.x)+sqr(pl[p].p.z-zed[i].p.z)<2.56f){
                            pl[p].health-=ZED_DAMAGE;
                            if(pl[p].health<0)
                                respawnPlayer(0);
                            if(p==plid)
                                updateHealthHud();
                            zed[i].state=Z_WANDERING;
                            zed[i].rot=rng.rand(M_PI*2);
                        }
                    switch(collideCharacter(i,false,zed[i].p.x,zed[i].p.y,zed[i].p.z,PL_RAD)){
                    case 0:
                        break;
                    case 2:
                        if(zed[i].v.y<CLIMB_SPEED)
                            zed[i].v.y=CLIMB_SPEED;
                        break;
                    default:
                        zed[i].state=Z_WANDERING;
                        zed[i].rot=rng.rand(M_PI*2);
                    }
                    if(zed[i].p.y>=0){
                        zed[i].p.y+=zed[i].v.y*t;
                        zed[i].v.y-=GRAVITY*t;
                    }else{
                        zed[i].p.y=0;
                        zed[i].v.y=0;
                    }
                    } break;
                case Z_HEALTH:
                case Z_AMMO:
                    zed[i].rot+=t*3.0f;
                    break;
                default:
                    break;
            }
        }

        return 0;
    }



    const int CEILING=15;
    const int DOORHEIGHT=6;
    inline void drawCeiling(int ix, int iy){
        glColor3f(0.55f,0.55f,0.55f);
        glVertex3i(ix*16,CEILING,iy*16); glVertex3i(ix*16,CEILING,(iy+1)*16);
        glVertex3i((ix+1)*16,CEILING,(iy+1)*16); glVertex3i((ix+1)*16,CEILING,iy*16);
    }
    inline void drawRoad(int ix, int iy, float col){
        glColor3f(col,col,col);
        glVertex3i(ix*16,0,iy*16); glVertex3i(ix*16,0,(iy+1)*16);
        glVertex3i((ix+1)*16,0,(iy+1)*16); glVertex3i((ix+1)*16,0,iy*16);
    }
    inline void drawHighWall(int ix, int iy, int jx, int jy){
        glColor3f(0.50f,0.50f,0.50f);
        glVertex3i(ix*16,0,iy*16); glVertex3i(jx*16,0,jy*16);
        glVertex3i(jx*16,CEILING*4,jy*16); glVertex3i(ix*16,CEILING*4,iy*16);
    }
    inline void drawWall(int ix, int iy, int jx, int jy){
        glColor3f(0.50f,0.50f,0.50f);
        glVertex3i(ix*16,0,iy*16); glVertex3i(jx*16,0,jy*16);
        glVertex3i(jx*16,CEILING,jy*16); glVertex3i(ix*16,CEILING,iy*16);
    }
    inline void drawDoor(int ix, int iy, int jx, int jy){
        const int xoff=(ix+ix+ix+jx)*4-ix*16;
        const int yoff=(iy+iy+iy+jy)*4-iy*16;
        glColor3f(0.50f,0.50f,0.50f);
        glVertex3i(ix*16,DOORHEIGHT,iy*16);
        glVertex3i(jx*16,DOORHEIGHT,jy*16);
        glVertex3i(jx*16,CEILING,jy*16);
        glVertex3i(ix*16,CEILING,iy*16);
        glVertex3i(ix*16,0,iy*16);
        glVertex3i(ix*16+xoff,0,iy*16+yoff);
        glVertex3i(ix*16+xoff,DOORHEIGHT,iy*16+yoff);
        glVertex3i(ix*16,DOORHEIGHT,iy*16);
        glVertex3i(ix*16+xoff*2,0,iy*16+yoff*2);
        glVertex3i(jx*16,0,jy*16);
        glVertex3i(jx*16,DOORHEIGHT,jy*16);
        glVertex3i(ix*16+xoff*2,DOORHEIGHT,iy*16+yoff*2);
    }

    int drawBuildings(){
        glBegin(GL_QUADS);
        drawHighWall(1,1,1,31);
        drawHighWall(1,31,31,31);
        drawHighWall(31,31,31,1);
        drawHighWall(31,1,1,1);
        for(int iy=1;iy<31;iy++)
        for(int ix=1;ix<31;ix++){
            if(map[iy*32+ix]&INSIDE_BIT){
                drawRoad(ix,iy,0.30f);
                drawCeiling(ix,iy);
                if(map[iy*32+ix]&DOORX_BIT) drawDoor(ix+1,iy,ix+1,iy+1);
                else drawWall(ix+1,iy,ix+1,iy+1);
                if(map[iy*32+ix]&DOORZ_BIT) drawDoor(ix,iy+1,ix+1,iy+1);
                else drawWall(ix,iy+1,ix+1,iy+1);
            }else{
                drawRoad(ix,iy,0.15f);
                if(map[iy*32+ix+1]&INSIDE_BIT){
                    if(map[iy*32+ix]&DOORX_BIT) drawDoor(ix+1,iy,ix+1,iy+1);
                    else drawWall(ix+1,iy,ix+1,iy+1);
                }
                if(map[iy*32+ix+32]&INSIDE_BIT){
                    if(map[iy*32+ix]&DOORZ_BIT) drawDoor(ix,iy+1,ix+1,iy+1);
                    else drawWall(ix,iy+1,ix+1,iy+1);
                }
            }
        }
        glEnd();
        return 0;
    }

    int drawZeds(){
        const float ZEDW=0.80f;
        const float ZEDW2=0.565685425f;
        const float HW=0.30;
        for(int i=0;i<MAX_ZEDS;i++) if(zed[i].state!=Z_NONE){
            glPushMatrix();
            glTranslatef(zed[i].p.x,zed[i].p.y,zed[i].p.z);
            glRotatef(-zed[i].rot*180.0f/M_PI,0.0f,1.0f,0.0f);
            switch(zed[i].state){
            case Z_DEAD:
                glColor3f(0.35f,0.35f,0.35f);
                glBegin(GL_QUAD_STRIP);
                glVertex3f(-1.4f,0.0f,+ZEDW2);    glVertex3f(+1.4f,0.0f,+ZEDW2);
                glVertex3f(-1.4f,ZEDW2*2,+ZEDW2); glVertex3f(+1.4f,ZEDW2*2,+ZEDW2);
                glVertex3f(-1.4f,ZEDW2*2,-ZEDW2); glVertex3f(+1.4f,ZEDW2*2,-ZEDW2);
                glVertex3f(-1.4f,0.0f,-ZEDW2);    glVertex3f(+1.4f,0.0f,-ZEDW2);
                glVertex3f(-1.4f,0.0f,+ZEDW2);    glVertex3f(+1.4f,0.0f,+ZEDW2);
                glEnd();
                glBegin(GL_QUADS);
                glVertex3f(-1.4f,0.0f,+ZEDW2);    glVertex3f(-1.4f,ZEDW2*2,+ZEDW2);
                glVertex3f(-1.4f,ZEDW2*2,-ZEDW2); glVertex3f(-1.4f,0.0f,-ZEDW2);
                glVertex3f(+1.4f,0.0f,+ZEDW2);    glVertex3f(+1.4f,ZEDW2*2,+ZEDW2);
                glVertex3f(+1.4f,ZEDW2*2,-ZEDW2); glVertex3f(+1.4f,0.0f,-ZEDW2);
                glEnd();
                break;
            case Z_HEALTH:
                glColor3f(0.76f,0.76f,0.76f);
                glScalef(HW,HW,HW);
                glTranslatef(0.0f,1.0f,0.0f);
                glBegin(GL_QUADS);
                glVertex3f(-0.5f,0.0f,+0.5f); glVertex3f(+0.5f,0.0f,+0.5f);
                glVertex3f(+0.5f,1.0f,+0.5f); glVertex3f(-0.5f,1.0f,+0.5f);
                glVertex3f(-1.5f,1.0f,+0.5f); glVertex3f(+1.5f,1.0f,+0.5f);
                glVertex3f(+1.5f,2.0f,+0.5f); glVertex3f(-1.5f,2.0f,+0.5f);
                glVertex3f(-0.5f,2.0f,+0.5f); glVertex3f(+0.5f,2.0f,+0.5f);
                glVertex3f(+0.5f,3.0f,+0.5f); glVertex3f(-0.5f,3.0f,+0.5f);
                glVertex3f(+0.5f,0.0f,-0.5f); glVertex3f(-0.5f,0.0f,-0.5f);
                glVertex3f(-0.5f,1.0f,-0.5f); glVertex3f(+0.5f,1.0f,-0.5f);
                glVertex3f(+1.5f,1.0f,-0.5f); glVertex3f(-1.5f,1.0f,-0.5f);
                glVertex3f(-1.5f,2.0f,-0.5f); glVertex3f(+1.5f,2.0f,-0.5f);
                glVertex3f(+0.5f,2.0f,-0.5f); glVertex3f(-0.5f,2.0f,-0.5f);
                glVertex3f(-0.5f,3.0f,-0.5f); glVertex3f(+0.5f,3.0f,-0.5f);
                glEnd();
                glBegin(GL_QUAD_STRIP);
                glVertex3f(-0.5f,0.0f,+0.5f); glVertex3f(-0.5f,0.0f,-0.5f);
                glVertex3f(+0.5f,0.0f,+0.5f); glVertex3f(+0.5f,0.0f,-0.5f);
                glVertex3f(+0.5f,1.0f,+0.5f); glVertex3f(+0.5f,1.0f,-0.5f);
                glVertex3f(+1.5f,1.0f,+0.5f); glVertex3f(+1.5f,1.0f,-0.5f);
                glVertex3f(+1.5f,2.0f,+0.5f); glVertex3f(+1.5f,2.0f,-0.5f);
                glVertex3f(+0.5f,2.0f,+0.5f); glVertex3f(+0.5f,2.0f,-0.5f);
                glVertex3f(+0.5f,3.0f,+0.5f); glVertex3f(+0.5f,3.0f,-0.5f);
                glVertex3f(-0.5f,3.0f,+0.5f); glVertex3f(-0.5f,3.0f,-0.5f);
                glVertex3f(-0.5f,2.0f,+0.5f); glVertex3f(-0.5f,2.0f,-0.5f);
                glVertex3f(-1.5f,2.0f,+0.5f); glVertex3f(-1.5f,2.0f,-0.5f);
                glVertex3f(-1.5f,1.0f,+0.5f); glVertex3f(-1.5f,1.0f,-0.5f);
                glVertex3f(-0.5f,1.0f,+0.5f); glVertex3f(-0.5f,1.0f,-0.5f);
                glVertex3f(-0.5f,0.0f,+0.5f); glVertex3f(-0.5f,0.0f,-0.5f);
                glEnd();
                break;
            case Z_AMMO:
                glColor3f(0.76f,0.76f,0.76f);
                glScalef(HW,HW,HW);
                glTranslatef(0.0f,1.0f,0.0f);
                glBegin(GL_TRIANGLE_STRIP);
                glVertex3f(-0.5f,3.0f,+0.5f); glVertex3f(+1.5f,1.0f,+0.5f); glVertex3f(-1.5f,2.0f,+0.5f);
                glVertex3f(+1.5f,0.0f,+0.5f); glVertex3f(+0.5f,0.0f,+0.5f);
                glEnd();
                glBegin(GL_TRIANGLE_STRIP);
                glVertex3f(+1.5f,1.0f,-0.5f); glVertex3f(-0.5f,3.0f,-0.5f); glVertex3f(-1.5f,2.0f,-0.5f);
                glVertex3f(+1.5f,0.0f,-0.5f); glVertex3f(+0.5f,0.0f,-0.5f);
                glEnd();
                glBegin(GL_QUAD_STRIP);
                glVertex3f(-0.5f,3.0f,+0.5f); glVertex3f(-0.5f,3.0f,-0.5f);
                glVertex3f(+1.5f,1.0f,+0.5f); glVertex3f(+1.5f,1.0f,-0.5f);
                glVertex3f(+1.5f,0.0f,+0.5f); glVertex3f(+1.5f,0.0f,-0.5f);
                glVertex3f(+0.5f,0.0f,+0.5f); glVertex3f(+0.5f,0.0f,-0.5f);
                glVertex3f(-1.5f,2.0f,+0.5f); glVertex3f(-1.5f,2.0f,-0.5f);
                glVertex3f(-0.5f,3.0f,+0.5f); glVertex3f(-0.5f,3.0f,-0.5f);
                glEnd();
                break;
            default:
                glColor3f(0.35f,0.35f,0.35f);
                glBegin(GL_QUAD_STRIP);
                glVertex3f(+ZEDW,0.0f,0.0f); glVertex3f(+ZEDW,2.8f,0.0f);
                glVertex3f(0.0f,0.0f,+ZEDW); glVertex3f(0.0f,2.8f,+ZEDW);
                glVertex3f(-ZEDW,0.0f,0.0f); glVertex3f(-ZEDW,2.8f,0.0f);
                glVertex3f(0.0f,0.0f,-ZEDW); glVertex3f(0.0f,2.8f,-ZEDW);
                glVertex3f(+ZEDW,0.0f,0.0f); glVertex3f(+ZEDW,2.8f,0.0f);
                glEnd();
                glBegin(GL_QUADS);
                glVertex3f(+ZEDW,0.0f,0.0f); glVertex3f(0.0f,0.0f,+ZEDW);
                glVertex3f(-ZEDW,0.0f,0.0f); glVertex3f(0.0f,0.0f,-ZEDW);
                glVertex3f(+ZEDW,2.8f,0.0f); glVertex3f(0.0f,2.8f,+ZEDW);
                glVertex3f(-ZEDW,2.8f,0.0f); glVertex3f(0.0f,2.8f,-ZEDW);
                glEnd();
                break;
            }
            glPopMatrix();
        }
        return 0;
    }

    int drawPlayers(){
        const float ZEDW=0.80f;
        for(int i=0;i<MAX_PLAYERS;i++)
            if(pl[i].state && i!=plid){
                glPushMatrix();
                glTranslatef(pl[i].p.x,pl[i].p.y,pl[i].p.z);
                glRotatef(-pl[i].lookr*180.0f/M_PI,0.0f,1.0f,0.0f);

                glColor3f(0.55f,0.55f,0.55f);
                glBegin(GL_QUAD_STRIP);
                glVertex3f(+ZEDW,0.0f,0.0f); glVertex3f(+ZEDW,2.8f,0.0f);
                glVertex3f(0.0f,0.0f,+ZEDW); glVertex3f(0.0f,2.8f,+ZEDW);
                glVertex3f(-ZEDW,0.0f,0.0f); glVertex3f(-ZEDW,2.8f,0.0f);
                glVertex3f(0.0f,0.0f,-ZEDW); glVertex3f(0.0f,2.8f,-ZEDW);
                glVertex3f(+ZEDW,0.0f,0.0f); glVertex3f(+ZEDW,2.8f,0.0f);
                glEnd();
                glBegin(GL_QUADS);
                glVertex3f(+ZEDW,0.0f,0.0f); glVertex3f(0.0f,0.0f,+ZEDW);
                glVertex3f(-ZEDW,0.0f,0.0f); glVertex3f(0.0f,0.0f,-ZEDW);
                glVertex3f(+ZEDW,2.8f,0.0f); glVertex3f(0.0f,2.8f,+ZEDW);
                glVertex3f(-ZEDW,2.8f,0.0f); glVertex3f(0.0f,2.8f,-ZEDW);
                glEnd();

                glPopMatrix();
            }
        return 0;
    }

    int drawParticles(){
	glEnable(GL_CULL_FACE);
        glColor3f(0.76f,0.76f,0.76f);
        for(int i=0;i<MAX_PARTICLES;i++)
            if(particles[i].p.x!=-1){
                const float x=particles[i].p.x;
                const float y=particles[i].p.y;
                const float z=particles[i].p.z;
                const float S=0.60f*particles[i].age;
                glBegin(GL_QUAD_STRIP);
                glVertex3f(x+S,y+S,z+S); glVertex3f(x+S,y-S,z+S); glVertex3f(x+S,y+S,z-S); glVertex3f(x+S,y-S,z-S);
                glVertex3f(x-S,y+S,z-S); glVertex3f(x-S,y-S,z-S); glVertex3f(x-S,y+S,z+S); glVertex3f(x-S,y-S,z+S);
                glVertex3f(x+S,y+S,z+S); glVertex3f(x+S,y-S,z+S);
                glEnd();
                glBegin(GL_QUADS);
                glVertex3f(x-S,y-S,z-S); glVertex3f(x+S,y-S,z-S); glVertex3f(x+S,y-S,z+S); glVertex3f(x-S,y-S,z+S);
                glVertex3f(x-S,y+S,z-S); glVertex3f(x-S,y+S,z+S); glVertex3f(x+S,y+S,z+S); glVertex3f(x+S,y+S,z-S);
                glEnd();
            }
	glDisable(GL_CULL_FACE);
        return 0;
    }

    int renderFrame(){
        if(!isserver && gamestate==0){
            glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glOrtho(0,WINDOW_W,0,WINDOW_H,-1.0f,1.0f);
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();

            glColor3f(0.40f,0.40f,0.40f);
            glBegin(GL_QUADS);
            glVertex2i(WINDOW_W/2-28,WINDOW_H/2-20); glVertex2i(WINDOW_W/2-28,WINDOW_H/2+4);
            glVertex2i(WINDOW_W/2-4,WINDOW_H/2+4); glVertex2i(WINDOW_W/2-4,WINDOW_H/2-20);
            glVertex2i(WINDOW_W/2+4,WINDOW_H/2-4); glVertex2i(WINDOW_W/2+4,WINDOW_H/2+20);
            glVertex2i(WINDOW_W/2+28,WINDOW_H/2+20); glVertex2i(WINDOW_W/2+28,WINDOW_H/2-4);
            glVertex2i(WINDOW_W/2-4,WINDOW_H/2-14); glVertex2i(WINDOW_W/2-4,WINDOW_H/2-10);
            glVertex2i(WINDOW_W/2+16,WINDOW_H/2-10); glVertex2i(WINDOW_W/2+16,WINDOW_H/2-14);
            glVertex2i(WINDOW_W/2+16,WINDOW_H/2-14); glVertex2i(WINDOW_W/2+16,WINDOW_H/2-4);
            glVertex2i(WINDOW_W/2+20,WINDOW_H/2-4); glVertex2i(WINDOW_W/2+20,WINDOW_H/2-14);
            glEnd();

            SDL_GL_SwapBuffers();
            return 0;
        }

	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(FOV,(float)WINDOW_W/(float)WINDOW_H,0.25f,1000.0f);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
        gluLookAt(cam.x,cam.y,cam.z,look.x,look.y,look.z,0.0f,1.0f,0.0f);

	glEnable(GL_DEPTH_TEST);
        drawBuildings();
        drawZeds();
        drawPlayers();
        drawParticles();
	glDisable(GL_DEPTH_TEST);

        //hud
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
        glOrtho(0,WINDOW_W,0,WINDOW_H,-1.0f,1.0f);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE,GL_ONE);
        glColor3f(0.40f,0.40f,0.40f);
        //crosshair
        glBegin(GL_QUADS);
        glVertex2i(WINDOW_W/2-1,WINDOW_H/2-1);
        glVertex2i(WINDOW_W/2-1,WINDOW_H/2+1);
        glVertex2i(WINDOW_W/2+1,WINDOW_H/2+1);
        glVertex2i(WINDOW_W/2+1,WINDOW_H/2-1);
        glEnd();
        //health
        const int POSX=25;
        const int POSY=25;
        glBegin(GL_POINTS);
        for(int iy=0;iy<20;iy++)
        for(int ix=0;ix<20;ix++)
            if(healthhud[iy*20+ix])
                glVertex2i(POSX+ix*2,POSY+iy*2);
        glEnd();
        //ammo
        glBegin(GL_LINES);
        if(plid!=-1)
            for(int i=0;i<pl[plid].ammo;i++){
                glVertex2f(WINDOW_W-POSX-(i%30)*4,POSY+(i/30)*10);
                glVertex2f(WINDOW_W-POSX-(i%30)*4,POSY+(i/30)*10+8);
            }
        glEnd();
        glDisable(GL_BLEND);

        SDL_GL_SwapBuffers();
        frames++;

        //SOIL_save_screenshot(capturefile,SOIL_SAVE_TYPE_TGA,0,0,WINDOW_W,WINDOW_H);

        return 0;
    }

}

