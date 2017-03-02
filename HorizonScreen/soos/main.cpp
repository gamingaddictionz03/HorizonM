#include <platform.hpp>

extern "C"
{
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <setjmp.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <malloc.h>
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <poll.h>
#include <arpa/inet.h>
#include <netdb.h>
}

#include <exception>

using ::abs;
using namespace std;

#define FPSNO 5

float fps = 0.0F;
u32 fpsticks[FPSNO];
u32 fpstick = 0;
int currwrite = 0;
int oldwrite = 0;

#define errfail(func) { printf("\n" #func " fail: (%i) %s\n", errno, strerror(errno)); goto killswitch; }


int pollsock(int sock, int wat, int timeout = 0)
{
    struct pollfd pd;
    pd.fd = sock;
    pd.events = wat;
    
    if(poll(&pd, 1, timeout) == 1)
        return pd.revents & wat;
    
    return 0;
}

class bufsoc
{
public:
    
    struct packet
    {
        u32 packetid : 8;
        u32 size : 24;
        u8 data[0];
    };
    
    int sock;
    u8* buf;
    int bufsize;
    int recvsize;
    
    bufsoc(int sock, int bufsize = 1024 * 1024)
    {
        this->bufsize = bufsize;
        buf = new u8[bufsize];
        
        recvsize = 0;
        this->sock = sock;
    }
    
    ~bufsoc()
    {
        delete[] buf;
    }
    
    int avail()
    {
        return pollsock(sock, POLLIN) == POLLIN;
    }
    
    int readbuf(int flags = 0)
    {
        u32 hdr = 0;
        int ret = recv(sock, &hdr, 4, flags);
        if(ret < 0) return -errno;
        if(ret < 4) return -1;
        *(u32*)buf = hdr;
        
        packet* p = pack();
        
        int mustwri = p->size;
        int offs = 4;
        while(mustwri)
        {
            ret = recv(sock, buf + offs , mustwri, flags);
            if(ret <= 0) return -errno;
            mustwri -= ret;
            offs += ret;
        }
        
        recvsize = offs;
        return offs;
    }
    
    int wribuf(int flags = 0)
    {
        int mustwri = pack()->size + 4;
        int offs = 0;
        int ret = 0;
        while(mustwri)
        {
            ret = send(sock, buf + offs , mustwri, flags);
            if(ret < 0) return -errno;
            mustwri -= ret;
            offs += ret;
        }
        
        return offs;
    }
    
    packet* pack()
    {
        return (packet*)buf;
    }
    
    int errformat(char* c, ...)
    {
        char* wat = nullptr;
        int len = 0;
        
        va_list args;
        va_start(args, c);
        len = vasprintf(&wat, c, args);
        va_end(args);
        
        if(len < 0)
        {
            puts("out of memory");
            return -1;
        }
        
        packet* p = pack();
        
        printf("Packet error %i: %s\n", p->packetid, wat);
        
        p->data[0] = p->packetid;
        p->packetid = 1;
        p->size = len + 2;
        strcpy((char*)(p->data + 1), wat);
        delete wat;
        
        return wribuf();
    }
};


int PumpEvent()
{
    SDL_Event evt;
    
    int i;
    int j;
    
    while(SDL_PollEvent(&evt))
    {
        switch(evt.type)
        {
            case SDL_QUIT:
            case SDL_APP_TERMINATING:
                return 0;
        }
    }
    
    return 1;
}




SDL_Window* win = 0;
SDL_Renderer* rendertop = 0;
SDL_Texture* tex[2] = {0, 0};
SDL_Surface* img[2] = {0, 0};


int port = 6464;
int sock = 0;
struct sockaddr_in sao;
socklen_t sizeof_sao = sizeof(sao);
bufsoc* soc = 0;
bufsoc::packet* p = 0;

u32* pdata = 0;


u8 sbuf[256 * 400 * 4 * 2];
u8 mehbuf[256 * 400 * 4 * 2];
int pixfmt[2] = {SDL_PIXELFORMAT_RGB565, SDL_PIXELFORMAT_RGB565};
int srcfmt[2] = {3, 3};
int stride[2] = {480, 480};
int bsiz[2] = {2, 2};
int ret = 0;


int main(int argc, char** argv)
{
    if(argc < 2)
    {
        printf("%s <IP address>\n", argv[0]);
        return 1;
    }
    
    memset(fpsticks, 0, sizeof(fpsticks));
    inet_aton(argv[1], &sao.sin_addr);
    
    sao.sin_family = AF_INET;
    sao.sin_port = htons(port);
    
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if(sock < 0) errfail(socket);
    soc = new bufsoc(sock, 0x200000);
    p = soc->pack();
    
    ret = connect(sock, (sockaddr*)&sao, sizeof_sao);
    if(ret < 0) errfail(connect); 
    
    puts("Connected");
    
    //SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
#ifdef _WIN32
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
#endif
    
    if(SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        puts("Failed to init SDL");
        goto killswitch;
    }
    
    win = SDL_CreateWindow("HorizonScreen " BUILDTIME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 720, 240, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
    if(!win)
    {
        printf("Can't create window: %s\n", SDL_GetError());
        goto killswitch;
    }
    
    rendertop = SDL_CreateRenderer(win, -1, /*SDL_RENDERER_PRESENTVSYNC*/0);
    
    img[0] = SDL_CreateRGBSurfaceWithFormat(0, 240, 400, 32, pixfmt[0]);
    img[1] = SDL_CreateRGBSurfaceWithFormat(0, 240, 320, 32, pixfmt[1]);
    
    SDL_RenderSetLogicalSize(rendertop, 720, 240);
    
    do
    {
        int i = sizeof(sbuf) >> 2;
        u32* _1 = (u32*)sbuf;
        while(i--)
        {
            *(_1++) = rand();
        }
    }
    while(0);
    
    while(PumpEvent())
    {
        if(!soc->avail()) goto nocoffei;
        
        ret = soc->readbuf();
        if(ret <= 0) errfail(soc->readbuf);
        
        switch(p->packetid)
        {
            case 2:
                pdata = (u32*)p->data;
                
                printf("ModeTOP: %04X (o: %i, bytesize: %i)\n", pdata[0], pdata[0] & 7, pdata[1]);
                printf("ModeBOT: %04X (o: %i, bytesize: %i)\n", pdata[2], pdata[2] & 7, pdata[3]);
                
                srcfmt[0] = pdata[0];
                stride[0] = pdata[1];
                srcfmt[1] = pdata[2];
                stride[1] = pdata[3];
                
                bsiz[0] = stride[0] / 240;
                bsiz[1] = stride[1] / 240;
                
                switch(pdata[0] & 7)
                {
                    case 0:
                        pixfmt[0] = SDL_PIXELFORMAT_RGBA8888;
                        break;
                    case 2:
                        pixfmt[0] = SDL_PIXELFORMAT_RGB565;
                        break;
                    case 3:
                        pixfmt[0] = SDL_PIXELFORMAT_RGBA5551;
                        break;
                    case 4:
                        pixfmt[0] = SDL_PIXELFORMAT_RGBA4444;
                        break;
                    default:
                        pixfmt[0] = SDL_PIXELFORMAT_BGR24;
                        break;
                }
                
                switch(pdata[2] & 7)
                {
                    case 0:
                        pixfmt[1] = SDL_PIXELFORMAT_RGBA8888;
                        break;
                    case 2:
                        pixfmt[1] = SDL_PIXELFORMAT_RGB565;
                        break;
                    case 3:
                        pixfmt[1] = SDL_PIXELFORMAT_RGBA5551;
                        break;
                    case 4:
                        pixfmt[1] = SDL_PIXELFORMAT_RGBA4444;
                        break;
                    default:
                        pixfmt[1] = SDL_PIXELFORMAT_BGR24;
                        break;
                }
                
                SDL_FreeSurface(img[0]);
                SDL_FreeSurface(img[1]);
                
                img[0] = SDL_CreateRGBSurfaceWithFormat(0, stride[0] / bsiz[0], 400, bsiz[0] << 3, pixfmt[0]);
                img[1] = SDL_CreateRGBSurfaceWithFormat(0, stride[1] / bsiz[1], 320, bsiz[1] << 3, pixfmt[1]);
                break;
            
            case 3:
            {
                memcpy(sbuf + *(u32*)p->data, p->data + 4, p->size);
                
                if(!*(u32*)p->data)
                {
                    u32 prev = SDL_GetTicks() - fpstick;
                    fpsticks[currwrite++] = prev;
                    fpstick = SDL_GetTicks();
                    if(currwrite == FPSNO) currwrite = 0;
                }
                break;
            }
            
            case 0xFF:
            {
                printf("DebugMSG (0x%X):", p->size);
                int i = 0;
                while(i < p->size)
                {
                    printf(" %08X", *(u32*)&p->data[i]);
                    i += 4;
                }
                putchar('\n');
                
                break;
            }
            
            default:
                printf("Unknown packet: %i\n", p->packetid);
                break;
        }
        
        
        nocoffei:
        
        SDL_LockSurface(img[0]);
        memcpy(img[0]->pixels, sbuf, stride[0] * 400);
        SDL_UnlockSurface(img[0]);
        
        SDL_LockSurface(img[1]);
        memcpy(img[1]->pixels, sbuf + (256 * 400 * 4), stride[1] * 320);
        SDL_UnlockSurface(img[1]);
        
        SDL_DestroyTexture(tex[0]);
        tex[0] = SDL_CreateTextureFromSurface(rendertop, img[0]);
        SDL_DestroyTexture(tex[1]);
        tex[1] = SDL_CreateTextureFromSurface(rendertop, img[1]);
        
        SDL_Point center;
        center.x = 0;
        center.y = 0;
        
        SDL_Rect soos;
        soos.x = 0;
        soos.y = 0;
        soos.w = 240;
        soos.h = 400;
        
        SDL_Rect dest;
        dest.x = 0;
        dest.y = 240;
        dest.w = 240;
        dest.h = 400;
        SDL_RenderCopyEx(rendertop, tex[0], &soos, &dest, 270.0F, &center, SDL_FLIP_NONE);
        
        soos.x = 0;
        soos.y = 0;
        soos.w = 240;
        soos.h = 320;
        
        dest.x = 400;
        dest.y = 240;
        dest.w = 240;
        dest.h = 320;
        SDL_RenderCopyEx(rendertop, tex[1], &soos, &dest, 270.0F, &center, SDL_FLIP_NONE);
        
        SDL_RenderPresent(rendertop);
        
        if(oldwrite != currwrite)
        {
            float currfps = 0.0F;
            for(int i = 0; i != FPSNO; i++) currfps += fpsticks[i];
            currfps /= FPSNO;
            fps = 1000.0F / currfps;
            printf("FPS: %f\n", fps);
            
            oldwrite = currwrite;
        }
    }
    
    killswitch:
    
    if(soc) delete soc;
    
    if(tex[0]) SDL_DestroyTexture(tex[0]);
    if(tex[1]) SDL_DestroyTexture(tex[1]);
    if(img[0]) SDL_FreeSurface(img[0]);
    if(img[1]) SDL_FreeSurface(img[1]);
    
    if(rendertop) SDL_DestroyRenderer(rendertop);    
    if(win) SDL_DestroyWindow(win);
    SDL_Quit();
    
    return 0;
}
