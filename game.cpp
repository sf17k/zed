#include <SDL/SDL.h>
#include <SDL/SDL_opengl.h>
#include <iostream>
#include <math.h>
#include "MersenneTwister.h"

using namespace std;

namespace Game{

    const int WINDOW_W=800;
    const int WINDOW_H=600;
    const int CENTER_X=WINDOW_W/2;
    const int CENTER_Y=WINDOW_H/2;

    SDL_Surface *screen=NULL;
    bool isKeyPressed[SDLK_LAST];
    bool isKeyDown[SDLK_LAST];

    GLfloat camx,camy,camz;
    GLfloat lookr,lookp;
    GLfloat lookx,looky,lookz;
    float sensitivity;
    float shootdelay;
    int health,ammo;
    char healthhud[400];

    MTRand rng;
    char *map=NULL;
    int *cols=NULL;
    const char INSIDE_BIT=0x01;
    const char DOORX_BIT=0x02;
    const char DOORZ_BIT=0x04;

    const int MAX_ZEDS=4096;
    const int MAX_BULLETS=64;
    const int MAX_PARTICLES=1024;
    const unsigned char Z_NONE=0;
    const unsigned char Z_DEAD=1;
    const unsigned char Z_IDLE=2; //hiding
    const unsigned char Z_WANDERING=3; //wandering about
    const unsigned char Z_ATTACKING=4; //running at players
    const unsigned char Z_HEALTH=10; //pickup
    const unsigned char Z_AMMO=11; //pickup

    struct Zed{
        unsigned char state;
        float x,y,z,rot;
        int ix,iz;
        int cnext,cprev;
    }zed[MAX_ZEDS];
    struct Bullet{
        float x,y,z,vx,vy,vz;
    }bullets[MAX_BULLETS];
    struct Particle{
        float age,x,y,z;
    }particles[MAX_PARTICLES];

    void updateHealthHud(){
        const float r=(float)health/100.0f;
        for(int i=0;i<400;i++)
            healthhud[i]=rng()<r?1:0;
    }

    void respawnPlayer(){
        camx=248.0;
        camy=2.5;
        camz=248.0;
        lookr=0.0;
        lookp=0.0;
        lookx=0.0;
        looky=0.0;
        lookz=0.0;
        shootdelay=0.5f;
        health=100;
        ammo=30;
        updateHealthHud();
    }

    int init(){
        //sdl
        if(SDL_Init(SDL_INIT_VIDEO)!=0){
            cerr<<"unable to initialize SDL: "<<SDL_GetError()<<endl;
            return 1;
        }
        atexit(SDL_Quit);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
        screen=SDL_SetVideoMode(WINDOW_W,WINDOW_H,32,SDL_OPENGL);
        if(screen==NULL){
            cerr<<"unable to set video mode: "<<SDL_GetError()<<endl;
            return 1;
        }
        SDL_WM_SetCaption("zed","zed");
        SDL_ShowCursor(SDL_DISABLE);

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
        sensitivity=0.0012f;
        respawnPlayer();

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
                            zed[c].x=posx+jx;
                            zed[c].z=posz+jz;
                            zed[c].y=0.0f;
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
            //if(rng()<0.10)
                zed[c].state=Z_WANDERING;
            //else
                //zed[z].state=Z_RETREATING;
            zed[c].x=ix*16+8;
            zed[c].y=0.0f;
            zed[c].z=iz*16+8;
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
            bullets[i].x=-1;
        for(int i=0;i<MAX_PARTICLES;i++)
            particles[i].x=-1;

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
        const int ix=zed[i].x/16.0f;
        const int iz=zed[i].z/16.0f;
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

    int collideCharacter(const int me, float& x, float& z, const float rad){
        const float lastx=x;
        const float lastz=z;
        if(x<16.0f+rad) x=16.0f+rad;
        if(z<16.0f+rad) z=16.0f+rad;
        if(x>31.0f*16.0f-rad) x=31.0f*16.0f-rad;
        if(z>31.0f*16.0f-rad) z=31.0f*16.0f-rad;
        const int ix=x/16.0f;
        const int iz=z/16.0f;
        const int i=iz*32+ix;
        const float offx=x-ix*16.0f;
        const float offz=z-iz*16.0f;
        //zeds
        //TODO: check adjacent blocks
        //const int jx=offx<8.0f?ix-1:ix+1;
        //const int jz=offz<8.0f?iz-1:iz+1;
        int c=cols[iz*32+ix];
        while(c!=-1){
            if(c!=me && sqr(x-zed[c].x)+sqr(z-zed[c].z)<2.56f){
                const float dist=sqrtf(sqr(x-zed[c].x)+sqr(z-zed[c].z));
                x+=(1.60f-dist)*(x-zed[c].x)/dist;
                z+=(1.60f-dist)*(z-zed[c].z)/dist;
            }
            c=zed[c].cnext;
        }
        //walls
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
        if(x!=lastx || z!=lastz)
            return 1;
        return 0;
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
        //TODO: check adjacent block
        const float maxdistsq=sqr(sqrt(halfdx*halfdx+halfdy*halfdy+halfdz*halfdz)+0.80f);
        int c=cols[iz2*32+ix2];
        while(c!=-1){
            if(sqr(x-zed[c].x)+sqr(z-zed[c].z)<maxdistsq){
                float nearx,neary,nearz;
                if(abs(dx)>abs(dz)){
                    nearx=((zed[c].z-z)*dx*dz+x*dz*dz+zed[c].x*dx*dx)/(dx*dx+dz*dz);
                    const float p=(nearx-x)/dx;
                    nearz=z+dz*p;
                    neary=y+dy*p;
                }else{
                    nearz=((zed[c].x-x)*dx*dz+z*dx*dx+zed[c].z*dz*dz)/(dx*dx+dz*dz);
                    const float p=(nearz-z)/dz;
                    nearx=x+dx*p;
                    neary=y+dy*p;
                }
                const float dist=sqrtf(sqr(zed[c].x-nearx)+sqr(zed[c].z-nearz));
                //this neary check isn't entirely accurate
                if(dist<0.80f && neary<2.8f+(0.80f-dist)*abs(dy/sqrtf(dx*dx+dz*dz))){
                    return c;
                }
            }
            c=zed[c].cnext;
        }
        return -1;
    }

    void killZed(const int i){
        const float RAD=0.80f;
        int count=12;
        for(int j=0;j<MAX_PARTICLES;j++)
            if(particles[j].x==-1){
                particles[j].x=zed[i].x+rng.rand(RAD*2)-RAD;
                particles[j].y=zed[i].y+rng.rand(2.8f);
                particles[j].z=zed[i].z+rng.rand(RAD*2)-RAD;
                particles[j].age=rng.rand(0.20f)+0.20f;
                if((--count)<=0)
                    break;
            }
        if(zed[i].cnext!=-1)
            zed[zed[i].cnext].cprev=zed[i].cprev;
        if(zed[i].cprev!=-1)
            zed[zed[i].cprev].cnext=zed[i].cnext;
        else
            cols[zed[i].iz*32+zed[i].ix]=zed[i].cnext;
        zed[i].cprev=-1;
        zed[i].cnext=-1;
        zed[i].state=Z_NONE;
    }

    int updateFrame(){
        const float GRAVITY=25.0f;
        const float WALK_SPEED=10.0f;
        const float BULLET_SPEED=70.0f;
        const float PARTICLE_AGE=0.10f;
        const float PARTICLE_INTERVAL=1.0f;
        const float PL_RAD=0.80f;
        const float ZED_RANGE=32.0f;

        const Uint32 time=SDL_GetTicks();
        static Uint32 timeLast=time;
        const float t=(float)(time-timeLast)/1000.0f;
        timeLast=time;
        if(t<=0)
            return 0;

        //player movement
        float dirx=0;
        float diry=0;
        if(keyDown(SDLK_w)) diry+=1.0f;
        if(keyDown(SDLK_a)) dirx-=1.0f;
        if(keyDown(SDLK_s)) diry-=1.0f;
        if(keyDown(SDLK_d)) dirx+=1.0f;
        if(dirx!=0 && diry!=0){
            dirx*=0.70710678;
            diry*=0.70710678;
        }
        camx+=WALK_SPEED*(diry*cosf(lookr)-dirx*sinf(lookr))*t;
        camz+=WALK_SPEED*(dirx*cosf(lookr)+diry*sinf(lookr))*t;
        collideCharacter(-1,camx,camz,PL_RAD);
        const float aimx=cosf(lookr)*cosf(lookp);
        const float aimy=sinf(lookp);
        const float aimz=sinf(lookr)*cosf(lookp);
        lookx=camx+aimx;
        looky=camy+aimy;
        lookz=camz+aimz;

        //pickup
        if(keyPressed(SDLK_e) && (health<100 || ammo<120))
            for(int i=0;i<MAX_ZEDS;i++)
                if((zed[i].state==Z_HEALTH || zed[i].state==Z_AMMO)
                    && sqr(camx-zed[i].x)+sqr(camz-zed[i].z)<PL_RAD*PL_RAD*4){
                    if(zed[i].state==Z_HEALTH){
                        if(health<100){
                            health+=25;
                            if(health>100)
                                health=100;
                            updateHealthHud();
                            zed[i].state=Z_NONE;
                            break;
                        }
                    }else{
                        if(ammo<120){
                            ammo+=30;
                            if(ammo>120)
                                ammo=120;
                            zed[i].state=Z_NONE;
                            break;
                        }
                    }
                }

        //shooting
        if(shootdelay>0)
            shootdelay-=t;
        unsigned char mb=SDL_GetMouseState(NULL,NULL);
        if(ammo>0 && mb&SDL_BUTTON(1) && shootdelay<=0){
            for(int i=0;i<MAX_BULLETS;i++)
                if(bullets[i].x==-1){
                    bullets[i].x=camx+aimx*0.75f;
                    bullets[i].y=camy+aimy*0.75f-0.3f;
                    bullets[i].z=camz+aimz*0.75f;
                    bullets[i].vx=aimx*BULLET_SPEED;
                    bullets[i].vy=aimy*BULLET_SPEED+0.15f*GRAVITY;
                    bullets[i].vz=aimz*BULLET_SPEED;
                    ammo--;
                    break;
                }
            shootdelay=0.20f;
        }

        //bullets
        for(int i=0;i<MAX_BULLETS;i++)
            if(bullets[i].x!=-1){
                const float dx=bullets[i].vx*t;
                const float dy=bullets[i].vy*t;
                const float dz=bullets[i].vz*t;
                int c;
                if((c=collideLine(bullets[i].x,bullets[i].y,bullets[i].z,dx,dy,dz))!=-1){
                    if(c>=0){ //hit zed
                        killZed(c);
                        /*switch(zed[c].state){
                        case Z_DEAD: killZed(c); break;
                        case Z_IDLE: zed[c].state=Z_DEAD; break;
                        default: zed[c].state=Z_IDLE; break;
                        }*/
                    }
                    for(int j=0;j<MAX_PARTICLES;j++)
                        if(particles[j].x==-1){
                            particles[j].x=bullets[i].x;
                            particles[j].y=bullets[i].y;
                            particles[j].z=bullets[i].z;
                            particles[j].age=PARTICLE_AGE*4.0f;
                            break;
                        }
                    bullets[i].x=-1;
                    continue;
                }
                bullets[i].vy-=GRAVITY*t;
                float dist=sqrtf(dx*dx+dy*dy+dz*dz);
                for(int j=0;j<MAX_PARTICLES;j++)
                    if(particles[j].x==-1){
                        particles[j].x=bullets[i].x+dx*dist;
                        particles[j].y=bullets[i].y+dy*dist;
                        particles[j].z=bullets[i].z+dz*dist;
                        particles[j].age=PARTICLE_AGE;
                        if((dist-=PARTICLE_INTERVAL)<=0)
                            break;
                    }
                bullets[i].x+=dx;
                bullets[i].y+=dy;
                bullets[i].z+=dz;
            }

        //particles
        for(int i=0;i<MAX_PARTICLES;i++)
            if(particles[i].x!=-1){
                particles[i].age-=t;
                if(particles[i].age<0)
                    particles[i].x=-1;
            }

        //zeds
        for(int i=0;i<MAX_ZEDS;i++) if(zed[i].state!=Z_NONE){
            switch(zed[i].state){
                case Z_DEAD: break;
                case Z_IDLE: break;
                case Z_WANDERING: {
                    //wander aimlessly
                    zed[i].x+=WALK_SPEED*1.5f*cosf(zed[i].rot)*t;
                    zed[i].z+=WALK_SPEED*1.5f*sinf(zed[i].rot)*t;
                    if(collideCharacter(i,zed[i].x,zed[i].z,PL_RAD)){
                        if(sqr(camx-zed[i].x)+sqr(camz-zed[i].z)<ZED_RANGE*ZED_RANGE){
                            zed[i].state=Z_ATTACKING;
                            zed[i].rot=atan2f(camz-zed[i].z,camx-zed[i].x);
                        }else{
                            zed[i].rot=rng.rand(M_PI*2);
                        }
                    }else if(sqr(camx-zed[i].x)+sqr(camz-zed[i].z)<2.56f){
                        const float dist=sqrtf(sqr(zed[i].x-camx)+sqr(zed[i].z-camz));
                        zed[i].x+=(1.60f-dist)*(zed[i].x-camx)/dist;
                        zed[i].z+=(1.60f-dist)*(zed[i].z-camz)/dist;
                        zed[i].rot=rng.rand(M_PI*2);
                    }
                    } break;
                case Z_ATTACKING: {
                    zed[i].x+=WALK_SPEED*1.5f*cosf(zed[i].rot)*t;
                    zed[i].z+=WALK_SPEED*1.5f*sinf(zed[i].rot)*t;
                    if(sqr(camx-zed[i].x)+sqr(camz-zed[i].z)<2.56f){
                        health-=30;
                        if(health<0)
                            respawnPlayer();
                        updateHealthHud();
                        zed[i].state=Z_WANDERING;
                        zed[i].rot=rng.rand(M_PI*2);
                    }
                    if(collideCharacter(i,zed[i].x,zed[i].z,PL_RAD)){
                        zed[i].state=Z_WANDERING;
                        zed[i].rot=rng.rand(M_PI*2);
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
        const float HW=0.30;
        for(int i=0;i<MAX_ZEDS;i++) if(zed[i].state!=Z_NONE){
            glPushMatrix();
            glTranslatef(zed[i].x,zed[i].y,zed[i].z);
            glRotatef(zed[i].rot*180.0f/M_PI,0.0f,-1.0f,0.0f);
            switch(zed[i].state){
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
                glVertex3f(ZEDW,0.0f,0.0f);  glVertex3f(ZEDW,2.8f,0.0f);
                glVertex3f(0.0f,0.0f,ZEDW);  glVertex3f(0.0f,2.8f,ZEDW);
                glVertex3f(-ZEDW,0.0f,0.0f); glVertex3f(-ZEDW,2.8f,0.0f);
                glVertex3f(0.0f,0.0f,-ZEDW); glVertex3f(0.0f,2.8f,-ZEDW);
                glVertex3f(ZEDW,0.0f,0.0f);  glVertex3f(ZEDW,2.8f,0.0f);
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
            if(particles[i].x!=-1){
                const float x=particles[i].x;
                const float y=particles[i].y;
                const float z=particles[i].z;
                const float S=0.50f*particles[i].age;
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
	gluPerspective(110.0f,(float)WINDOW_W/(float)WINDOW_H,0.25f,1000.0f);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
        gluLookAt(camx,camy,camz,lookx,looky,lookz,0.0f,1.0f,0.0f);

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
        for(int i=0;i<ammo;i++){
            glVertex2f(WINDOW_W-POSX-(i%30)*4,POSY+(i/30)*10);
            glVertex2f(WINDOW_W-POSX-(i%30)*4,POSY+(i/30)*10+8);
        }
        glEnd();

        glDisable(GL_BLEND);

        /*
        const GLfloat wallw=0.075f;
	glEnable(GL_CULL_FACE);
        glBegin(GL_QUADS);
        for(int iy=1;iy<31;iy++)
        for(int ix=1;ix<31;ix++)
            if(map[iy*32+ix]&INSIDE_BIT){
                //X wall
                glColor3f(0.75f,0.75f,0.75f);
                glVertex3f(ix+1.0f-wallw,0.0f,iy);
                glVertex3f(ix+1.0f-wallw,0.0f,iy+1.0f);
                glVertex3f(ix+1.0f,0.0f,iy+1.0f);
                glVertex3f(ix+1.0f,0.0f,iy);
                //Z wall
                glVertex3f(ix,0.0f,iy+1.0f);
                glVertex3f(ix+1.0f,0.0f,iy+1.0f);
                glVertex3f(ix+1.0f,0.0f,iy+1.0f-wallw);
                glVertex3f(ix,0.0f,iy+1.0f-wallw);
            }
        for(int iy=0;iy<31;iy++)
        for(int ix=0;ix<31;ix++){
            //X wall
            if(map[iy*32+ix+1]&INSIDE_BIT){
                glColor3f(0.75f,0.75f,0.75f);
                glVertex3f(ix+1.0f-wallw,0.0f,iy-wallw);
                glVertex3f(ix+1.0f-wallw,0.0f,iy+1.0f);
                glVertex3f(ix+1.0f,0.0f,iy+1.0f);
                glVertex3f(ix+1.0f,0.0f,iy-wallw);
            }
            //Z wall
            if(map[iy*32+ix+32]&INSIDE_BIT){
                glColor3f(0.75f,0.75f,0.75f);
                glVertex3f(ix,0.0f,iy+1.0f);
                glVertex3f(ix+1.0f,0.0f,iy+1.0f);
                glVertex3f(ix+1.0f,0.0f,iy+1.0f-wallw);
                glVertex3f(ix,0.0f,iy+1.0f-wallw);
            }
            //doorways
            if(map[iy*32+ix]&DOORX_BIT){
                glColor3f(0.0f,0.0f,0.0f);
                glVertex3f(ix+1.0f-wallw,0.0f,iy+0.25f);
                glVertex3f(ix+1.0f-wallw,0.0f,iy+0.5f);
                glVertex3f(ix+1.0f,0.0f,iy+0.5f);
                glVertex3f(ix+1.0f,0.0f,iy+0.25f);
            }
            if(map[iy*32+ix]&DOORZ_BIT){
                glColor3f(0.0f,0.0f,0.0f);
                glVertex3f(ix+0.25f,0.0f,iy+1.0f);
                glVertex3f(ix+0.5f,0.0f,iy+1.0f);
                glVertex3f(ix+0.5f,0.0f,iy+1.0f-wallw);
                glVertex3f(ix+0.25f,0.0f,iy+1.0f-wallw);
            }
        }
        glEnd();
	glDisable(GL_CULL_FACE);
        */

        /*
	glEnable(GL_DEPTH_TEST);
        const GLfloat dist=6.0f;
        glBegin(GL_QUADS);
            glColor3f(0.75f,0.0f,0.0f);
            glVertex3f(dist,3.0f,1.0f); glVertex3f(dist,2.0f,0.0f);
            glVertex3f(dist,0.0f,2.0f); glVertex3f(dist,1.0f,3.0f);
            glVertex3f(dist,3.0f,2.0f); glVertex3f(dist,1.0f,0.0f);
            glVertex3f(dist,0.0f,1.0f); glVertex3f(dist,2.0f,3.0f);
            glColor3f(0.0f,0.75f,0.0f);
            glVertex3f(-3.0f,dist,1.0f); glVertex3f(-2.0f,dist,0.0f);
            glVertex3f(-1.0f,dist,1.0f); glVertex3f(-2.0f,dist,2.0f);
            glVertex3f(-3.0f,dist,2.0f); glVertex3f(-1.0f,dist,0.0f);
            glVertex3f(-0.0f,dist,1.0f); glVertex3f(-2.0f,dist,3.0f);
            glColor3f(0.0f,0.0f,0.75f);
            glVertex3f(-0.0f,3.0f,dist); glVertex3f(-0.0f,2.0f,dist);
            glVertex3f(-3.0f,2.0f,dist); glVertex3f(-3.0f,3.0f,dist);
            glVertex3f(-0.0f,1.0f,dist); glVertex3f(-0.0f,0.0f,dist);
            glVertex3f(-3.0f,0.0f,dist); glVertex3f(-3.0f,1.0f,dist);
            glVertex3f(-2.0f,3.0f,dist); glVertex3f(-0.0f,1.0f,dist);
            glVertex3f(-1.0f,0.0f,dist); glVertex3f(-3.0f,2.0f,dist);
        glEnd();
	glDisable(GL_DEPTH_TEST);
        */

        SDL_GL_SwapBuffers();

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
                    lookr+=mx*sensitivity;
                    if(lookr>M_PI*2) lookr-=M_PI*2;
                    if(lookr<0) lookr+=M_PI*2;
                    lookp-=my*sensitivity;
                    if(lookp>0.49f*M_PI) lookp=0.49f*M_PI;
                    if(lookp<-0.49f*M_PI) lookp=-0.49f*M_PI;
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
    Game::init();
    for(;;){
        if(Game::pollEvents() || Game::keyPressed(SDLK_F10))
            break;
        Game::updateFrame();
        Game::renderFrame();
    }
    return 0;
}

