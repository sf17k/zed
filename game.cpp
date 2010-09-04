#include <SDL/SDL.h>
#include <SDL/SDL_opengl.h>
#include <iostream>
#include <math.h>
#include "MersenneTwister.h"
#include "SOIL.h"

using namespace std;

namespace Game{

    const bool CAPTURE_VIDEO=false;
    const bool FULLSCREEN=false;
    const int WINDOW_W=CAPTURE_VIDEO?640:800;
    const int WINDOW_H=CAPTURE_VIDEO?480:600;
    const float FOV=105.0f;
    const int CENTER_X=WINDOW_W/2;
    const int CENTER_Y=WINDOW_H/2;
    const int CAPTURE_BLUR=3;
    const double CAPTURE_STEP=1.0/25.0/CAPTURE_BLUR;

    bool sdl_started=false;
    bool REPLAYING=false;

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
    }pl[MAX_PLAYERS];

    struct Zed{
        unsigned char state;
        vect p;
        vect v;
        float rot;
        int ix,iz;
        int cnext,cprev;
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

    struct FrameData{
        float lookr,lookp;
        unsigned char keys;
    };

    struct ReplayData{
        long unsigned int rngstate[MTRand::SAVE];
        int numframes;
        FrameData f[75*60*10]; //TODO: check for overflow
    };

    ReplayData* replay=NULL;

    void updateHealthHud(){
        const float r=(float)pl[0].health/100.0f;
        for(int i=0;i<400;i++)
            healthhud[i]=rng()<r?1:0;
    }

    void respawnPlayer(int p){
        cam.set(pl[p].p.set(248.0f,0.0f,248.0f));
        cam.y+=2.5f;
        pl[p].v.set(0.0f,0.0f,0.0f);
        pl[p].lookr=0.0f;
        pl[p].lookp=0.0f;
        look.set(0.0f,0.0f,0.0f);
        pl[p].shootdelay=0.5f;
        pl[p].health=100;
        pl[p].ammo=30;
        pl[p].onground=true;
        updateHealthHud();
    }

    int init(){
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
        if(CAPTURE_VIDEO){
            if(REPLAYING){
                replay->numframes=frames;
                rng.load(&replay->rngstate[0]);
            }else{
                if(replay) delete replay;
                replay=new ReplayData();
                rng.save(&replay->rngstate[0]);
            }
        }
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

        for(int i=0;i<MAX_BULLETS;i++)
            bullets[i].p.x=-1;
        for(int i=0;i<MAX_PARTICLES;i++)
            particles[i].p.x=-1;

        respawnPlayer(0);

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

    void makeOBB(OBB* b, const int c){
        const float ZEDW2=0.565685425f;
        if(c<0){
            b->p.set(pl[0].p);
            b->p.y+=1.4f;
            b->e.set(vect(ZEDW2,1.4f,ZEDW2));
            b->rotatey(-pl[0].lookr);
            return;
        }
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
    int collideCharacter(const int me, float& x, float& y, float& z, const float rad){
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
                if(c==me){
                    c=zed[c].cnext;
                    continue;
                }
                /*//obb-obb test
                OBB b;
                makeOBB(&b,c);
                if(intersectionOBB(a,b)){
                    const float HEIGHT=zed[c].p.y+(zed[c].state==Z_DEAD?1.13137085f:2.8f);
                    if(me<0){
                        if(pl[0].p.y>HEIGHT-0.3f && pl[0].p.y<HEIGHT){
                            if(pl[0].v.y<0){
                                pl[0].p.y=HEIGHT;
                                pl[0].v.y=0;
                            }
                            pl[0].onground=true;
                        }
                    }else{
                        if(zed[me].p.y>HEIGHT-0.3f && zed[me].p.y<HEIGHT){
                            if(zed[me].v.y<0){
                                zed[me].p.y=HEIGHT;
                                zed[me].v.y=0;
                            }
                        }
                    }
                    makeOBB(&a,me);
                }*/
                if(zed[c].state==Z_DEAD){
                    //upright-obb-upright-cylinder test
                    OBB b;
                    makeOBB(&b,c);
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
                            if(me<0){
                                if(pl[0].v.y<0){
                                    p.y=b.e.y; b.ltow(p);
                                    pl[0].p.y=p.y;
                                    pl[0].v.y=0;
                                }
                                pl[0].onground=true;
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
        if(me>=0)
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
                makeOBB(&b,c);
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

    int updateFrame(){
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

        const Uint32 time=SDL_GetTicks();
        static Uint32 timeLast=time;
        const float t=CAPTURE_VIDEO?CAPTURE_STEP:(float)(time-timeLast)/1000.0f;
        if(t<=0)
            return 0;
        if(CAPTURE_VIDEO && !REPLAYING){
            static float wait=0;
            float w=CAPTURE_STEP-(time-timeLast)/1000.0f;
            wait+=w;
            if(wait>0){
                SDL_Delay(wait*1000.0f);
                wait=(SDL_GetTicks()-time)/1000.0f;
            }
        }
        timeLast=time;

        //record/replay
        unsigned char mb=SDL_GetMouseState(NULL,NULL);
        bool K_LEFT=keyDown(SDLK_a);
        bool K_RIGHT=keyDown(SDLK_d);
        bool K_FORWARD=keyDown(SDLK_w);
        bool K_BACK=keyDown(SDLK_s);
        bool K_JUMP=keyDown(SDLK_SPACE);
        bool K_USE=keyPressed(SDLK_e);
        bool K_FIRE=mb&SDL_BUTTON(1);
        if(CAPTURE_VIDEO){
            if(REPLAYING){
                pl[0].lookr=replay->f[frames].lookr;
                pl[0].lookp=replay->f[frames].lookp;
                K_LEFT    =replay->f[frames].keys& KB_LEFT;
                K_RIGHT   =replay->f[frames].keys& KB_RIGHT;
                K_FORWARD =replay->f[frames].keys& KB_FORWARD;
                K_BACK    =replay->f[frames].keys& KB_BACK;
                K_JUMP    =replay->f[frames].keys& KB_JUMP;
                K_USE     =replay->f[frames].keys& KB_USE;
                K_FIRE    =replay->f[frames].keys& KB_FIRE;
            }else{
                replay->f[frames].lookr=pl[0].lookr;
                replay->f[frames].lookp=pl[0].lookp;
                replay->f[frames].keys|= K_LEFT    ? KB_LEFT    :0;
                replay->f[frames].keys|= K_RIGHT   ? KB_RIGHT   :0;
                replay->f[frames].keys|= K_FORWARD ? KB_FORWARD :0;
                replay->f[frames].keys|= K_BACK    ? KB_BACK    :0;
                replay->f[frames].keys|= K_JUMP    ? KB_JUMP    :0;
                replay->f[frames].keys|= K_USE     ? KB_USE     :0;
                replay->f[frames].keys|= K_FIRE    ? KB_FIRE    :0;
            }
        }

        //player movement
        float dirx=0;
        float diry=0;
        if(K_FORWARD) diry+=1.0f;
        if(K_LEFT) dirx-=1.0f;
        if(K_BACK) diry-=1.0f;
        if(K_RIGHT) dirx+=1.0f;
        if(dirx!=0 && diry!=0){
            dirx*=0.70710678;
            diry*=0.70710678;
        }
        const float lasty=pl[0].p.y;
        pl[0].v.x=WALK_SPEED*(diry*cosf(pl[0].lookr)-dirx*sinf(pl[0].lookr));
        pl[0].v.z=WALK_SPEED*(dirx*cosf(pl[0].lookr)+diry*sinf(pl[0].lookr));
        pl[0].p.adds(pl[0].v,t);
        if(pl[0].p.y>0){
            pl[0].v.y-=GRAVITY*t;
        }else{
            pl[0].p.y=0;
            pl[0].v.y=0;
            pl[0].onground=true;
        }
        if(collideCharacter(-1,pl[0].p.x,pl[0].p.y,pl[0].p.z,PL_RAD)==2){
            if(pl[0].v.y<CLIMB_SPEED)
                pl[0].v.y=CLIMB_SPEED;
            pl[0].onground=true;
        }
        if(K_JUMP && pl[0].onground){
            pl[0].v.y=JUMP_SPEED;
            pl[0].p.y+=0.01f;
            pl[0].onground=false;
        }
        if(pl[0].p.y<lasty)
            pl[0].onground=false;
        cam.set(pl[0].p);
        cam.y+=2.5f;
        const vect aim(cosf(pl[0].lookr)*cosf(pl[0].lookp),
                        sinf(pl[0].lookp),
                        sinf(pl[0].lookr)*cosf(pl[0].lookp));
        look.set(cam).add(aim);

        //pickup
        if(K_USE && (pl[0].health<100 || pl[0].ammo<120))
            for(int i=0;i<MAX_ZEDS;i++)
                if((zed[i].state==Z_HEALTH || zed[i].state==Z_AMMO)
                    && sqr(pl[0].p.x-zed[i].p.x)+sqr(pl[0].p.z-zed[i].p.z)<PL_RAD*PL_RAD*4){
                    if(zed[i].state==Z_HEALTH){
                        if(pl[0].health<100){
                            pl[0].health+=25;
                            if(pl[0].health>100)
                                pl[0].health=100;
                            updateHealthHud();
                            zed[i].state=Z_NONE;
                            break;
                        }
                    }else{
                        if(pl[0].ammo<120){
                            pl[0].ammo+=30;
                            if(pl[0].ammo>120)
                                pl[0].ammo=120;
                            zed[i].state=Z_NONE;
                            break;
                        }
                    }
                }

        //shooting
        if(pl[0].shootdelay>0)
            pl[0].shootdelay-=t;
        if(pl[0].ammo>0 && K_FIRE && pl[0].shootdelay<=0){
            for(int i=0;i<MAX_BULLETS;i++)
                if(bullets[i].p.x==-1){
                    bullets[i].p.set(pl[0].p).adds(aim,0.75f);
                    bullets[i].p.y+=2.5f-0.3f;
                    bullets[i].v.set(pl[0].v).adds(aim,BULLET_SPEED);
                    bullets[i].v.y+=0.15f*GRAVITY;
                    pl[0].ammo--;
                    break;
                }
            pl[0].shootdelay=SHOOT_DELAY;
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
                    switch(collideCharacter(i,zed[i].p.x,zed[i].p.y,zed[i].p.z,PL_RAD)){
                    case 0:
                        if(sqr(pl[0].p.x-zed[i].p.x)+sqr(pl[0].p.z-zed[i].p.z)<2.56f){
                            const float dist=sqrtf(sqr(zed[i].p.x-pl[0].p.x)+sqr(zed[i].p.z-pl[0].p.z));
                            zed[i].p.x+=(1.60f-dist)*(zed[i].p.x-pl[0].p.x)/dist;
                            zed[i].p.z+=(1.60f-dist)*(zed[i].p.z-pl[0].p.z)/dist;
                            zed[i].rot=rng.rand(M_PI*2);
                        }
                        break;
                    case 2:
                        if(zed[i].v.y<CLIMB_SPEED)
                            zed[i].v.y=CLIMB_SPEED;
                        break;
                    default:
                        if(sqr(pl[0].p.x-zed[i].p.x)+sqr(pl[0].p.z-zed[i].p.z)<ZED_RANGE*ZED_RANGE){
                            zed[i].state=Z_ATTACKING;
                            zed[i].rot=atan2f(pl[0].p.z-zed[i].p.z,pl[0].p.x-zed[i].p.x);
                        }else{
                            zed[i].rot=rng.rand(M_PI*2);
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
                    if(sqr(pl[0].p.x-zed[i].p.x)+sqr(pl[0].p.z-zed[i].p.z)<2.56f){
                        pl[0].health-=ZED_DAMAGE;
                        if(pl[0].health<0)
                            respawnPlayer(0);
                        updateHealthHud();
                        zed[i].state=Z_WANDERING;
                        zed[i].rot=rng.rand(M_PI*2);
                    }
                    switch(collideCharacter(i,zed[i].p.x,zed[i].p.y,zed[i].p.z,PL_RAD)){
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
        for(int i=0;i<pl[0].ammo;i++){
            glVertex2f(WINDOW_W-POSX-(i%30)*4,POSY+(i/30)*10);
            glVertex2f(WINDOW_W-POSX-(i%30)*4,POSY+(i/30)*10+8);
        }
        glEnd();
        glDisable(GL_BLEND);

        SDL_GL_SwapBuffers();
        frames++;

        if(CAPTURE_VIDEO && REPLAYING){
            glAccum(GL_ACCUM,1.0/CAPTURE_BLUR);
            if(frames%CAPTURE_BLUR==0){
                glAccum(GL_RETURN,1.0);
                //save frame
                static char capturefile[]="cap/0000.tga";
                SOIL_save_screenshot(capturefile,SOIL_SAVE_TYPE_TGA,0,0,WINDOW_W,WINDOW_H);
                for(int i=7;i>=4;i--) if(++capturefile[i]>'9') capturefile[i]='0'; else break;
                glClear(GL_ACCUM_BUFFER_BIT);
            }
        }

        return 0;
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
            case SDL_MOUSEMOTION: { //TODO: check if grabbed, ungrab on ESC
                int mx,my;
                SDL_GetMouseState(&mx,&my);
                mx-=CENTER_X;
                my-=CENTER_Y;
                if(mx!=0 || my!=0){
                    pl[0].lookr+=mx*sensitivity;
                    if(pl[0].lookr>M_PI*2) pl[0].lookr-=M_PI*2;
                    if(pl[0].lookr<0) pl[0].lookr+=M_PI*2;
                    pl[0].lookp-=my*sensitivity;
                    if(pl[0].lookp>0.49f*M_PI) pl[0].lookp=0.49f*M_PI;
                    if(pl[0].lookp<-0.49f*M_PI) pl[0].lookp=-0.49f*M_PI;
                    SDL_WarpMouse(CENTER_X,CENTER_Y);
                }
                } break;
            case SDL_QUIT:
                return 1;
            }
        }
        return 0;
    }

}

int main(){
    using namespace Game;
    init();
    for(;;){
        if(pollEvents() || keyPressed(SDLK_F10))
            break;
        if(CAPTURE_VIDEO && !REPLAYING && keyPressed(SDLK_F8)){
            replay->numframes=frames;
            REPLAYING=true;
            init();
        }
        updateFrame();
        renderFrame();
        if(REPLAYING && frames>=replay->numframes)
            break;
    }
    return 0;
}

