#include <SDL/SDL_net.h>
#include <iostream>
#include "game.h"
#include "net.h"
using namespace std;
using namespace Net;

const int DEFAULT_PORT=8080;
const int MAX_CLIENTS=8;

UDPsocket udpsock=NULL;

struct Client{
    IPaddress address;
    int state;
    int lasttime;
}clients[MAX_CLIENTS];

int sendWorld(int c, int part){
    if(!udpsock)
        return -1;
    if(part*256>32*32-256)
        return 0;
    unsigned char* map=Game::getMap();
    if(!map)
        return -1;

    UDPpacket *p=SDLNet_AllocPacket(258);
    if(!p){
        cout<<"SDLNet_AllocPacket: "<<SDLNet_GetError()<<"\n";
        return -1;
    }

    p->len=258;
    p->data[0]=P_WORLD;
    p->data[1]=part;
    for(int i=0;i<256;i++)
        p->data[i+2]=map[part*256+i];

    if(!SDLNet_UDP_Send(udpsock,c,p)){
        cout<<"SDLNet_UDP_Send: "<<SDLNet_GetError()<<"\n";
        return -1;
    }

    SDLNet_FreePacket(p);
    p=NULL;
    return 0;
}

int sendClientInfo(int c){
    if(!udpsock)
        return -1;

    UDPpacket *p=SDLNet_AllocPacket(2);
    if(!p){
        cout<<"SDLNet_AllocPacket: "<<SDLNet_GetError()<<"\n";
        return -1;
    }

    p->len=2;
    p->data[0]=P_CLIENTINFO;
    p->data[1]=c;

    if(!SDLNet_UDP_Send(udpsock,c,p)){
        cout<<"SDLNet_UDP_Send: "<<SDLNet_GetError()<<"\n";
        return -1;
    }

    SDLNet_FreePacket(p);
    p=NULL;
    return 0;
}

int sendPlayerUpdates(){
    if(!udpsock)
        return -1;

    UDPpacket *p=SDLNet_AllocPacket(21);
    if(!p){
        cout<<"SDLNet_AllocPacket: "<<SDLNet_GetError()<<"\n";
        return -1;
    }

    for(int i=0;i<MAX_CLIENTS;i++) if(clients[i].state==1){
        unsigned short pv[8];
        unsigned char kha[3];
        if(Game::getPlayerUpdate(i,&pv[0],&kha[0])==-1)
            continue;

        p->len=21;
        p->data[0]=P_PLAYERUPDATE;
        p->data[1]=(unsigned char)i;
        p->data[2]=kha[0];
        p->data[3]=kha[1];
        p->data[4]=kha[2];
        for(int j=0;j<8;j++)
            SDLNet_Write16(pv[j],&p->data[5+j*2]);

        for(int c=0;c<MAX_CLIENTS;c++) if(clients[c].state==1){
            if(!SDLNet_UDP_Send(udpsock,c,p)){
                cout<<"SDLNet_UDP_Send: "<<SDLNet_GetError()<<"\n";
                continue;
            }
        }
    }

    SDLNet_FreePacket(p);
    p=NULL;
    return 0;
}


int initServer(int port){
    if(SDL_Init(NULL)==-1){
        cout<<"SDL_Init: "<<SDL_GetError()<<"\n";
        return -1;
    }
    if(SDLNet_Init()==-1){
        cout<<"SDLNet_Init: "<<SDLNet_GetError()<<"\n";
        return -1;
    }

    udpsock=SDLNet_UDP_Open(port);
    if(!udpsock){
        cout<<"SDLNet_UDP_Open: "<<SDLNet_GetError()<<"\n";
        return -1;
    }

    for(int i=0;i<MAX_CLIENTS;i++)
        clients[i].state=0;

    return 0;
}

void closeServer(){
    SDLNet_UDP_Close(udpsock);
    udpsock=NULL;
    SDLNet_Quit();
    SDL_Quit();
}

int connectClient(IPaddress address){
    for(int i=0;i<MAX_CLIENTS;i++)
        if(clients[i].state==0){
            if(SDLNet_UDP_Bind(udpsock,i,&address)==-1){
                cout<<"SDLNet_UDP_Bind: "<<SDLNet_GetError()<<"\n";
                return -1;
            }
            clients[i].address=address;
            clients[i].state=2;
            clients[i].lasttime=SDL_GetTicks();
            cout<<"client connected\n";
            return i;
        }
    return -1;
}

void disconnectClient(int c){
    if(clients[c].state){
        SDLNet_UDP_Unbind(udpsock,c);
        clients[c].state=0;
        Game::removePlayer(c);
        cout<<"client disconnected\n";
    }
}

void processPacket(UDPpacket *p, int i){
    if(p->len<1)
        return;
    switch(p->data[0]){
    case P_UPDATE:
        if(p->len<6)
            break;
        Game::setKeys(i,p->data[1]);
        Game::setAim(i,SDLNet_Read16(&p->data[2]),SDLNet_Read16(&p->data[4]));
        break;
    case P_GETWORLD:
        if(p->len<2)
            break;
        if(clients[i].state!=2)
            break;
        sendWorld(i,p->data[1]);
        break;
    case P_GETCLIENTINFO:
        clients[i].state=1;
        Game::respawnPlayer(i);
        sendClientInfo(i);
        break;
    default:
        break;
    }
}

int updateServer(){
    const int NOW=SDL_GetTicks();

    UDPpacket *p=SDLNet_AllocPacket(1024);
    if(!p){
        cout<<"SDLNet_AllocPacket: "<<SDLNet_GetError()<<"\n";
        return -1;
    }

    while(SDLNet_UDP_Recv(udpsock,p)>0){
        int i=0;
        while(i<MAX_CLIENTS && !(clients[i].state && clients[i].address.host==p->address.host
            && clients[i].address.port==p->address.port))
            i++;
        if(i>=MAX_CLIENTS){
            if((i=connectClient(p->address))!=-1)
                processPacket(p,i);
        }else{
            processPacket(p,i);
            clients[i].lasttime=NOW;
        }
    }

    SDLNet_FreePacket(p);
    p=NULL;

    for(int i=0;i<MAX_CLIENTS;i++){
        if(clients[i].lasttime<NOW-5000)
            disconnectClient(i);
    }

    static int lasttick=NOW;
    if(lasttick<NOW-50){
        sendPlayerUpdates();
        lasttick=NOW;
    }

    return 0;
}

int main(){
    if(initServer(DEFAULT_PORT))
        return 0;
    Game::initServer();
    cout<<"server started\n";

    for(;;){
        if(updateServer())
            break;
        Game::updateFrame();
    }

    return 0;
}

