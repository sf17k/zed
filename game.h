#ifndef H_GAME
#define H_GAME

namespace Game{

    int initServer();
    int initClient();
    int updateFrame();
    int renderFrame();
    void respawnPlayer(int p);
    void removePlayer(int p);

    void setKeys(int i, unsigned char keys);
    void setAim(int i, unsigned short aimr, unsigned short aimp);
    int getClientUpdate(unsigned char *keys, unsigned short *aimr, unsigned short *aimp);
    unsigned char* getMap();
    void setClientID(int id);
    int getPlayerUpdate(int i, unsigned short *pv, unsigned char* kha);
    int setPlayerUpdate(int i, const unsigned short *pv, const unsigned char* kha);

}

#endif

