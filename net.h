#ifndef H_NET
#define H_NET

namespace Net{

    //client->server
    const unsigned char P_UPDATE=2;
    const unsigned char P_GETWORLD=3;
    const unsigned char P_GETCLIENTINFO=4;

    //server->client
    const unsigned char P_WORLD=2;
    const unsigned char P_CLIENTINFO=3;
    const unsigned char P_PLAYERUPDATE=4;

}

#endif

