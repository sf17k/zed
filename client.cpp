#include <SDL/SDL_net.h>
#include <iostream>
#include "game.h"
#include "net.h"
using namespace std;
using namespace Net;

const int DEFAULT_PORT=8080;

UDPsocket udpsock=NULL;

int connectstatus=0;
bool downloadedMapPart[4]={};

int sendClientUpdate(){
    if(!udpsock)
        return -1;
    UDPpacket *p=SDLNet_AllocPacket(6);
    if(!p){
        cout<<"SDLNet_AllocPacket: "<<SDLNet_GetError()<<"\n";
        return -1;
    }

    unsigned char keys;
    unsigned short aimr,aimp;
    if(Game::getClientUpdate(&keys,&aimr,&aimp))
        return 0;

    p->len=6;
    p->data[0]=P_UPDATE;
    p->data[1]=keys;
    SDLNet_Write16(aimr,&p->data[2]);
    SDLNet_Write16(aimp,&p->data[4]);

    if(!SDLNet_UDP_Send(udpsock,0,p)){
        cout<<"SDLNet_UDP_Send: "<<SDLNet_GetError()<<"\n";
        return -1;
    }
    SDLNet_FreePacket(p);
    p=NULL;
    return 0;
}

int sendConnectRequest(){
    if(!udpsock)
        return -1;
    UDPpacket *p=SDLNet_AllocPacket(2);
    if(!p){
        cout<<"SDLNet_AllocPacket: "<<SDLNet_GetError()<<"\n";
        return -1;
    }

    bool waiting=false;
    for(int part=0;part<4;part++)
        if(!downloadedMapPart[part]){
            waiting=true;
            p->len=2;
            p->data[0]=P_GETWORLD;
            p->data[1]=part;
            if(!SDLNet_UDP_Send(udpsock,0,p)){
                cout<<"SDLNet_UDP_Send: "<<SDLNet_GetError()<<"\n";
                return -1;
            }
        }
    if(!waiting){
        p->len=1;
        p->data[0]=P_GETCLIENTINFO;
        if(!SDLNet_UDP_Send(udpsock,0,p)){
            cout<<"SDLNet_UDP_Send: "<<SDLNet_GetError()<<"\n";
            return -1;
        }
    }

    SDLNet_FreePacket(p);
    p=NULL;
    return 0;
}

int initClient(const char* hostname, int port){
    if(SDL_Init(NULL)==-1){
        cout<<"SDL_Init: "<<SDL_GetError()<<"\n";
        return -1;
    }
    if(SDLNet_Init()==-1){
        cout<<"SDLNet_Init: "<<SDLNet_GetError()<<"\n";
        return -1;
    }

    udpsock=SDLNet_UDP_Open(0);
    if(!udpsock){
        cout<<"SDLNet_UDP_Open: "<<SDLNet_GetError()<<"\n";
        return -1;
    }

    IPaddress address;
    if(SDLNet_ResolveHost(&address,hostname,port)==-1){
        cout<<"SDLNet_ResolveHost: "<<SDLNet_GetError()<<"\n";
        return -1;
    }
    if(SDLNet_UDP_Bind(udpsock,0,&address)==-1){
        cout<<"SDLNet_UDP_Bind: "<<SDLNet_GetError()<<"\n";
        return -1;
    }

    sendConnectRequest();

    return 0;
}

void closeClient(){
    SDLNet_UDP_Close(udpsock);
    udpsock=NULL;
    SDLNet_Quit();
    SDL_Quit();
}

void processPacket(UDPpacket *p){
    if(p->len<1)
        return;
    switch(p->data[0]){
    case P_WORLD: {
        if(p->len<258)
            break;
        unsigned char *map=Game::getMap();
        if(!map)
            break;
        int start=p->data[1]*256;
        if(start>32*32-256)
            break;
        for(int i=0;i<256;i++)
            map[start+i]=p->data[i+2];
        downloadedMapPart[p->data[1]]=true;
        } break;
    case P_CLIENTINFO:
        if(p->len<2)
            break;
        Game::setClientID(p->data[1]);
        connectstatus=1;
        break;
    case P_PLAYERUPDATE: {
        if(p->len<21)
            break;
        int i=p->data[1];
        unsigned char kha[3];
        unsigned short pv[8];
        kha[0]=p->data[2];
        kha[1]=p->data[3];
        kha[2]=p->data[4];
        for(int j=0;j<8;j++)
            pv[j]=SDLNet_Read16(&p->data[5+j*2]);
        Game::setPlayerUpdate(i,&pv[0],&kha[0]);
        } break;
    default:
        break;
    }
}

int updateClient(){
    UDPpacket *p=SDLNet_AllocPacket(1024);
    if(!p){
        cout<<"SDLNet_AllocPacket: "<<SDLNet_GetError()<<"\n";
        return -1;
    }

    while(SDLNet_UDP_Recv(udpsock,p)>0){
        processPacket(p);
    }

    SDLNet_FreePacket(p);
    p=NULL;

    if(connectstatus==0){
        SDL_Delay(200);
        sendConnectRequest();
    }else{
        sendClientUpdate();
    }

    return 0;
}

int main(){
    if(initClient("localhost",DEFAULT_PORT))
        return 0;
    Game::initClient();

    for(;;){
        if(updateClient())
            break;
        if(Game::updateFrame())
            break;
        Game::renderFrame();
    }

    return 0;
}

