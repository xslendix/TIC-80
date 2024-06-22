#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>
#include <psprtc.h>

#include <pspprof.h>

#include <pspgu.h>
#include <pspgum.h>

#include <math.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "studio/fs.h"
#include "studio/studio.h"
#include "studio/system.h"

static unsigned int __attribute__((aligned(16))) list[262144];

#define BUF_WIDTH (512)
#define SCR_WIDTH (480)
#define SCR_HEIGHT (272)

PSP_MODULE_INFO("TIC-80", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);

typedef struct
{
    float u, v;
    u32 color;
    float x, y, z;
} TexVertex;

bool g_running = true;

int exit_callback(int arg1, int arg2, void* common)
{
    (void)arg1;
    (void)arg2;
    (void)common;
    g_running = false;
    return 0;
}

int callback_thread(SceSize args, void* argp)
{
    (void)args;
    (void)argp;
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

int setup_callbacks(void)
{
    int thid = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, 0);
    if (thid >= 0)
        sceKernelStartThread(thid, 0, 0);
    return thid;
}

static unsigned int static_offset = 0;

static unsigned int get_memory_size(unsigned int width, unsigned int height, unsigned int psm)
{
    switch (psm)
    {
        case GU_PSM_T4:
            return (width * height) >> 1;

        case GU_PSM_T8:
            return width * height;

        case GU_PSM_5650:
        case GU_PSM_5551:
        case GU_PSM_4444:
        case GU_PSM_T16:
            return 2 * width * height;

        case GU_PSM_8888:
        case GU_PSM_T32:
            return 4 * width * height;

        default:
            return 0;
    }
}

void* get_static_vram_buffer(unsigned int width, unsigned int height, unsigned int psm)
{
    unsigned int mem_sz = get_memory_size(width, height, psm);
    void* result = (void*)static_offset;
    static_offset += mem_sz;

    return result;
}

void* get_static_vram_texture(unsigned int width, unsigned int height, unsigned int psm)
{
    void* result = get_static_vram_buffer(width, height, psm);
    return (void*)(((unsigned int)result) + ((unsigned int)sceGeEdramGetAddr()));
}

void render_texture(float x, float y, float w, float h, u32 buffer[], int tw, int th)
{
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, tw, th, tw, buffer);
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);

    TexVertex* vertices = (TexVertex*)sceGuGetMemory(2 * sizeof(TexVertex));
    vertices[0] = (TexVertex){0, 0, 0, x, y, 0.0f};
    vertices[1] = (TexVertex){1, 1, 0, x + w, y + h, 0.0f};

    sceGumDrawArray(GU_SPRITES,
                    GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 2, 0, vertices);
}

u32* g_fb = NULL;

void tic_sys_clipboard_set(const char* text)
{
    (void)text;
}

bool tic_sys_clipboard_has()
{
    return 0;
}

char* tic_sys_clipboard_get()
{
    return NULL;
}

void tic_sys_clipboard_free(const char* text)
{
    (void)text;
}

u64 tic_sys_counter_get()
{
    u64 tick;
    sceRtcGetCurrentTick(&tick);
    return tick;
}

u64 tic_sys_freq_get()
{
    return (u64)sceRtcGetTickResolution();
}

void tic_sys_fullscreen_set(bool value)
{
    (void)value;
}

bool tic_sys_fullscreen_get()
{
}

void tic_sys_message(const char* title, const char* message)
{
}

void tic_sys_title(const char* title)
{
}

void tic_sys_open_path(const char* path)
{
}

void tic_sys_open_url(const char* url)
{
}

void tic_sys_preseed()
{
    srand(time(NULL));
    rand();
}

void tic_sys_update_config()
{
}

void tic_sys_default_mapping(tic_mapping* mapping)
{
    *mapping = (tic_mapping){
        tic_key_up,
        tic_key_down,
        tic_key_left,
        tic_key_right,

        tic_key_z, // a
        tic_key_x, // b
        tic_key_a, // x
        tic_key_s, // y
    };
}

bool tic_sys_keyboard_text(char* text)
{
    return false;
}

Studio* g_studio;
tic80_input g_input;

static void copy_frame(void)
{
    u32* in = studio_mem(g_studio)->product.screen;

    memcpy(g_fb, studio_mem(g_studio)->product.screen, TIC80_FULLWIDTH * TIC80_FULLHEIGHT * 4);
    sceKernelDcacheWritebackInvalidateRange(g_fb, TIC80_FULLWIDTH * TIC80_FULLHEIGHT * 4);
}

void process_thumbstick_input(float x, float y, float* normalizedX, float* normalizedY, float deadzone)
{
    float distance = sqrt(x * x + y * y);

    if (distance < deadzone)
    {
        *normalizedX = 0;
        *normalizedY = 0;
    }
    else
    {
        float normalizedDistance = (distance - deadzone) / (1 - deadzone);
        *normalizedX = (x / distance) * normalizedDistance;
        *normalizedY = (y / distance) * normalizedDistance;
    }
}

int main(int argc, char** argv)
{
    explicit_bzero(&g_input, sizeof g_input);

    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL)
        return 1;
    strcpy(cwd + strlen(cwd), "/EBOOT.PBP");

    char* argv2[] = {cwd, "--cmd=surf", 0};
    int argc_used = 2;
    char** argv_used = argv2;

    mkdir("tic80", S_IRWXU | S_IRWXG | S_IRWXO);

    setup_callbacks();
    pspDebugScreenInit();

    g_studio = studio_create(argc_used, argv_used, 44100, TIC80_PIXEL_COLOR_RGBA8888, "tic80", 0);
    if (!g_studio)
        return 1;

    scePowerUnlock(0);
    scePowerSetClockFrequency(333, 333, 166);

    g_fb = (u32*)get_static_vram_texture(TIC80_FULLWIDTH, TIC80_FULLHEIGHT, GU_PSM_8888);
    for (int x = 0; x < TIC80_FULLWIDTH; x++)
        for (int y = 0; y < TIC80_FULLHEIGHT; y++)
            g_fb[x + TIC80_FULLWIDTH * y] = 0xff000000;

    void* fbp0 = get_static_vram_buffer(BUF_WIDTH, SCR_HEIGHT, GU_PSM_8888);
    void* fbp1 = get_static_vram_buffer(BUF_WIDTH, SCR_HEIGHT, GU_PSM_8888);
    void* zbp = get_static_vram_buffer(BUF_WIDTH, SCR_HEIGHT, GU_PSM_4444);

    sceGuInit();

    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_8888, fbp0, BUF_WIDTH);
    sceGuDispBuffer(SCR_WIDTH, SCR_HEIGHT, fbp1, BUF_WIDTH);
    sceGuDepthBuffer(zbp, BUF_WIDTH);
    sceGuOffset(2048 - (SCR_WIDTH / 2), 2048 - (SCR_HEIGHT / 2));
    sceGuViewport(2048, 2048, SCR_WIDTH, SCR_HEIGHT);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_WIDTH, SCR_HEIGHT);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuAlphaFunc(GU_GREATER, 0, 0xff);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuFrontFace(GU_CW);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_CULL_FACE);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuFinish();
    sceGuSync(0, 0);

    sceDisplayWaitVblankStart();
    sceGuDisplay(1);

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    u32 fc = 0;
    SceCtrlData pad;
    while (g_running && !studio_alive(g_studio))
    {
        u32 start_frame = fc;

        sceCtrlReadBufferPositive(&pad, 1);
        g_input.gamepads.first.data = 0;
        g_input.gamepads.first.up = (pad.Buttons & PSP_CTRL_UP) != 0;
        g_input.gamepads.first.down = (pad.Buttons & PSP_CTRL_DOWN) != 0;
        g_input.gamepads.first.left = (pad.Buttons & PSP_CTRL_LEFT) != 0;
        g_input.gamepads.first.right = (pad.Buttons & PSP_CTRL_RIGHT) != 0;
        g_input.gamepads.first.a = (pad.Buttons & PSP_CTRL_CROSS) != 0;
        g_input.gamepads.first.b = (pad.Buttons & PSP_CTRL_CIRCLE) != 0;
        g_input.gamepads.first.x = (pad.Buttons & PSP_CTRL_SQUARE) != 0;
        g_input.gamepads.first.y = (pad.Buttons & PSP_CTRL_TRIANGLE) != 0;

        g_input.keyboard.data = 0;
        if (pad.Buttons & PSP_CTRL_START)
            g_input.keyboard.keys[0] = tic_key_escape;

        float x, y;
        process_thumbstick_input(pad.Lx, pad.Ly, &x, &y, 0.1);

        g_input.mouse.relative = 1;
        if (x != 0)
            g_input.mouse.rx = x;
        if (y != 0)
            g_input.mouse.ry = y;

        studio_tick(g_studio, g_input);

        if (getStudioMode(g_studio) == TIC_CONSOLE_MODE)
            setStudioMode(g_studio, TIC_SURF_MODE);

        copy_frame();

        sceGuStart(GU_DIRECT, list);

        sceGuClearColor(0xff000000);
        sceGuClear(GU_COLOR_BUFFER_BIT);

        sceGumMatrixMode(GU_PROJECTION);
        sceGumLoadIdentity();
        sceGumOrtho(0, 480, 272, 0, -1, 1);

        sceGumMatrixMode(GU_VIEW);
        sceGumLoadIdentity();

        sceGumMatrixMode(GU_MODEL);
        sceGumLoadIdentity();
        //render_texture(-TIC80_MARGIN_LEFT-TIC80_MARGIN_RIGHT, -TIC80_MARGIN_TOP - TIC80_MARGIN_BOTTOM, TIC80_FULLWIDTH*2, TIC80_FULLHEIGHT*2, g_fb, TIC80_FULLWIDTH, TIC80_FULLHEIGHT);
        render_texture(0, 0, TIC80_FULLWIDTH, TIC80_FULLHEIGHT, g_fb, TIC80_FULLWIDTH, TIC80_FULLHEIGHT);

        sceGuFinish();
        sceGuSync(0, 0);

        pspDebugScreenSetOffset((int)fbp0);
        pspDebugScreenSetXY(0, 0);

        sceDisplayWaitVblank();
        fbp0 = sceGuSwapBuffers();

        fc++;
    }

    studio_delete(g_studio);
    sceGuTerm();

    sceKernelExitGame();
    return 0;
}

int ftruncate(int fd, off_t length)
{
    // Get the current file size
    off_t current_size = lseek(fd, 0, SEEK_END);
    if (current_size == -1)
    {
        return -1;
    }

    // Truncate the file if needed
    if (length < current_size)
    {
        if (ftruncate(fd, length) == -1)
        {
            return -1;
        }
    }
    else if (length > current_size)
    {
        // Extend the file size
        if (lseek(fd, length - 1, SEEK_SET) == -1)
        {
            return -1;
        }
        if (write(fd, "", 1) != 1)
        {
            return -1;
        }
    }

    return 0;
}
