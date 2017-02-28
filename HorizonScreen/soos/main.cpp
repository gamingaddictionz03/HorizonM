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

static jmp_buf __exc;
static int  __excno;

#define FPSNO 10

float fps = -1.0F;
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
            
            /*
            case SDL_WINDOWEVENT:
                switch(evt.window.event)
                {
                    case SDL_WINDOWEVENT_MINIMIZED:
                        while(SDL_WaitEvent(&evt))
                        {
                            switch(evt.type)
                            {
                                case SDL_QUIT:
                                case SDL_APP_TERMINATING:
                                    return 0;   
                            }
                            if(evt.type != SDL_WINDOWEVENT) continue;
                            if(evt.window.event == SDL_WINDOWEVENT_RESTORED) break;
                        }
                        
                        break;
                        
                    //case SDL_WINDOWEVENT_RESIZED:
                    //    puts("Window resized");                        
                    //    break;
                    
                    default:
                        //printf("Window event: %i\n", evt.window.event);
                        
                        break;
                }
                
                break;*/
                
            default:
                //printf("SDL Event: %i\n", evt.type);
                
                break;
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
u8 sbuf[400 * 240 * 4 * 2];
u8 mehbuf[400 * 240 * 4 * 2];
struct sockaddr_in sao;
socklen_t sizeof_sao = sizeof(sao);
uint64_t dummy[2] = {0, 0};
bufsoc* soc = 0;
int pixfmt[2] = {SDL_PIXELFORMAT_RGB888, SDL_PIXELFORMAT_RGB888};
int bsiz[2] = {3, 3};
bufsoc::packet* p = 0;
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
    
    SDL_RenderSetLogicalSize(rendertop, 400 + 320, 240);
    
    ret = connect(sock, (sockaddr*)&sao, sizeof_sao);
    if(ret < 0) errfail(connect); 
    
    puts("Connected");
    
    while(PumpEvent())
    {
        if(!soc->avail()) goto nocoffei;
        
        ret = soc->readbuf();
        if(ret <= 0) errfail(soc->readbuf);
        
        switch(p->packetid)
        {
            case 2:
                printf("ModeTOP: %i (o: %i, bytesize: %i)\n", p->data[0], p->data[0] & 7, p->data[1]);
                printf("ModeBOT: %i (o: %i, bytesize: %i)\n", p->data[2], p->data[2] & 7, p->data[3]);
                bsiz[0] = p->data[1];
                pixfmt[0] = SDL_PIXELFORMAT_RGB24;
                
                switch(p->data[0] & 7)
                {
                    case 0:
                        pixfmt[0] = SDL_PIXELFORMAT_RGBA8888;
                        bsiz[0] = 4;
                        break;
                    case 2:
                        pixfmt[0] = SDL_PIXELFORMAT_RGB565;
                        bsiz[0] = 2;
                        break;
                    case 3:
                        pixfmt[0] = SDL_PIXELFORMAT_RGBA5551;
                        bsiz[0] = 2;
                        break;
                    case 4:
                        pixfmt[0] = SDL_PIXELFORMAT_RGBA4444;
                        bsiz[0] = 2;
                        break;
                    default:
                        pixfmt[0] = SDL_PIXELFORMAT_BGR24;
                        bsiz[0] = 3;
                        break;
                }
                
                bsiz[1] = p->data[3];
                pixfmt[1] = SDL_PIXELFORMAT_RGB24;
                
                switch(p->data[2] & 7)
                {
                    case 0:
                        pixfmt[1] = SDL_PIXELFORMAT_RGBA8888;
                        bsiz[1] = 4;
                        break;
                    case 2:
                        pixfmt[1] = SDL_PIXELFORMAT_RGB565;
                        bsiz[1] = 2;
                        break;
                    case 3:
                        pixfmt[1] = SDL_PIXELFORMAT_RGBA5551;
                        bsiz[1] = 2;
                        break;
                    case 4:
                        pixfmt[1] = SDL_PIXELFORMAT_RGBA4444;
                        bsiz[1] = 2;
                        break;
                    default:
                        pixfmt[1] = SDL_PIXELFORMAT_BGR24;
                        bsiz[1] = 3;
                        break;
                }
                
                SDL_FreeSurface(img[0]);
                SDL_FreeSurface(img[1]);
                img[0] = SDL_CreateRGBSurfaceWithFormat(0, 240, 400, bsiz[0] << 3, pixfmt[0]);
                img[1] = SDL_CreateRGBSurfaceWithFormat(0, 240, 320, bsiz[1] << 3, pixfmt[1]);
                break;
            
            case 3:
            {
                memcpy(sbuf + *(u32*)p->data, p->data + 4, p->size);
                
                if(!*(u32*)p->data)
                {
                    u32 prev = SDL_GetTicks() - fpstick;
                    fpsticks[currwrite++] = prev;
                    fpstick = SDL_GetTicks();
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
        memcpy(img[0]->pixels, sbuf, 240 * 400 * bsiz[0]);
        SDL_UnlockSurface(img[0]);
        
        SDL_LockSurface(img[1]);
        memcpy(img[1]->pixels, sbuf + (240 * 400 * 4), 240 * 320 * bsiz[1]);
        SDL_UnlockSurface(img[1]);
        
        SDL_DestroyTexture(tex[0]);
        tex[0] = SDL_CreateTextureFromSurface(rendertop, img[0]);
        SDL_DestroyTexture(tex[1]);
        tex[1] = SDL_CreateTextureFromSurface(rendertop, img[1]);
        
        SDL_Point center;
        center.x = 0;
        center.y = 0;
        
        SDL_Rect dest;
        dest.x = 0;
        dest.y = 240;
        dest.w = 240;
        dest.h = 400;
        SDL_RenderCopyEx(rendertop, tex[0], nullptr, &dest, 270.0F, &center, SDL_FLIP_NONE);
        
        dest.x = 400;
        dest.y = 240;
        dest.w = 240;
        dest.h = 320;
        SDL_RenderCopyEx(rendertop, tex[1], nullptr, &dest, 270.0F, &center, SDL_FLIP_NONE);
        
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
