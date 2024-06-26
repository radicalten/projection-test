#include <stdio.h>
#include <stdlib.h>
#include <gccore.h>
#include <malloc.h>
#include <string.h>
#include <wiiuse/wpad.h>

#include "texture.h"

#define FIFO_SIZE (256*1024)

static void *xfb = NULL;
static GXRModeObj *rmode = NULL;
static GXTexObj texture;
static Mtx44 proj;

static void load_texture()
{
    u32 size = GX_GetTexBufferSize(width, height, GX_TF_RGBA8, GX_FALSE, 0);
    uint8_t *texels = memalign(32, size);
    char *data = header_data;
    int tex_pitch = width;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            char pixel[3];
            HEADER_PIXEL(data, pixel);
            u32 offset = (((y >> 2) << 4) * tex_pitch) +
                ((x >> 2) << 6) + (((y % 4 << 2) + x % 4) << 1);
            texels[offset] = 0xff;
            texels[offset + 1] = pixel[0];
            texels[offset + 32] = pixel[1];
            texels[offset + 33] = pixel[2];
        }
    }
    DCStoreRange(texels, size);
    GX_InvalidateTexAll();

    GX_InitTexObj(&texture, texels, width, height,
                  GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjLOD(&texture, GX_NEAR, GX_NEAR, 0., 0., 0.,
                     GX_FALSE, GX_FALSE, GX_ANISO_1);
    GX_LoadTexObj(&texture, GX_TEXMAP0);
}

static void draw_square()
{
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2s16(-2, -2);
    GX_Position2s16(-2, 2);
    GX_Position2s16(2, 2);
    GX_Position2s16(2, -2);
    GX_End();
}

static void setup_viewport()
{
    u32 w, h;

    w = rmode->fbWidth;
    h = rmode->efbHeight;

    printf("width %d, height %d\n", w, h);
    // matrix, t, b, l, r, n, f
    guPerspective(proj, 45, (f32)w/h, 0.1F, 300.0F);
    //guOrtho(proj, 1, -2, -1, 2, 0.1F, 300.0F);
    GX_LoadProjectionMtx(proj, GX_PERSPECTIVE);

    GX_SetViewport(0, 0, w, h, 0, 1);

    f32 yscale = GX_GetYScaleFactor(h, rmode->xfbHeight);
    GX_SetDispCopyYScale(yscale);
    GX_SetScissor(0, 0, w, h);
    GX_SetDispCopySrc(0, 0, w, h);
    GX_SetDispCopyDst(w, rmode->xfbHeight);
    GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
}

//---------------------------------------------------------------------------------
int main(int argc, char **argv) {
//---------------------------------------------------------------------------------

	// Initialise the video system
	VIDEO_Init();

	// This function initialises the attached controllers
	WPAD_Init();

	// Obtain the preferred video mode from the system
	// This will correspond to the settings in the Wii menu
	rmode = VIDEO_GetPreferredMode(NULL);

	// Allocate memory for the display in the uncached region
	xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));

	// Set up the video registers with the chosen mode
	VIDEO_Configure(rmode);

	// Tell the video hardware where our display memory is
	VIDEO_SetNextFramebuffer(xfb);

	// Make the display visible
	VIDEO_SetBlack(false);

	// Flush the video register changes to the hardware
	VIDEO_Flush();

	// Wait for Video setup to complete
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();

    void *fifoBuffer = NULL;
    fifoBuffer = MEM_K0_TO_K1(memalign(32,FIFO_SIZE));
    memset(fifoBuffer, 0, FIFO_SIZE);
    GX_Init(fifoBuffer, FIFO_SIZE);

    setup_viewport();
    load_texture();

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_S16, 0);

    GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GXColor backgroundColor = {255, 0, 0, 255};
    GX_SetCopyClear(backgroundColor, GX_MAX_Z24);

    GX_SetNumChans(0);
    GX_SetNumTexGens(1);
    Mtx pm = {
        {-0.5, 0, 0.5, 0},
        {0, 0.5, 0.5, 0},
        {0, 0, 1, 0},
    };
    GX_LoadTexMtxImm(pm, GX_DTTMTX0, GX_MTX3x4);
    GX_SetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX3x4, GX_TG_POS, GX_TEXMTX0, GX_FALSE, GX_DTTMTX0);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
    GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);

    GX_SetCullMode(GX_CULL_NONE);
    GX_SetColorUpdate(GX_TRUE);

    Mtx view;
    guVector cam = {0.0F, 0.0F, 0.0F},
             up = {0.0F, 1.0F, 0.0F},
             look = {0.0F, 0.0F, -1.0F};
    guLookAt(view, &cam, &up, &look);

    float rtri = 0.0f;
    guVector Xaxis = {1,0,0};
    guVector Yaxis = {0,1,0};
    guVector Zaxis = {0,0,1};
	while(1) {
        Mtx model, modelview, rot;
        guMtxIdentity(model);
        guMtxRotAxisDeg(model, &Yaxis, rtri);
        guMtxRotAxisDeg(rot, &Xaxis, rtri);
        guMtxConcat(rot,model,model);
        guMtxRotAxisDeg(rot, &Zaxis, rtri);
        guMtxConcat(rot,model,model);
        guMtxTransApply(model, model, 0.0f,0.0f,-6.0f);
        guMtxConcat(view,model,modelview);
        // load the modelview matrix into matrix memory
        GX_LoadPosMtxImm(modelview, GX_PNMTX0);

        Mtx m, trans;
        guMtxCopy(modelview, m);
        guMtxScale(trans, proj[0][0], proj[1][1], 1);
        guMtxConcat(trans, m, m);
        GX_LoadTexMtxImm(m, GX_TEXMTX0, GX_MTX3x4);
        draw_square();

		// Call WPAD_ScanPads each loop, this reads the latest controller states
		WPAD_ScanPads();

		// WPAD_ButtonsDown tells us which buttons were pressed in this loop
		// this is a "one shot" state which will not fire again until the button has been released
		u32 pressed = WPAD_ButtonsDown(0);

		// We return to the launcher application via exit
		if ( pressed & WPAD_BUTTON_HOME ) exit(0);

        GX_DrawDone();
        GX_CopyDisp(xfb,GX_TRUE);

		// Wait for the next frame
		VIDEO_WaitVSync();
        rtri+=0.5f;
	}

	return 0;
}
