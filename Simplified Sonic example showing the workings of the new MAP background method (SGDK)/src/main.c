/*
Pigsy, March 2021
A cut down/simplified version of the Sonic sample included in version 1.62 of SGDK.
The alternative (and older) method of displaying large background maps has been removed, 
along with a few other superfluous functions, in order to provide a clearer example of how the 
new MAP method works.
*/

#include <genesis.h>

#include "gfx.h"
#include "sprite.h"
#include "sound.h"
#include "dma.h"

#define SFX_JUMP            64
#define SFX_ROLL            65
#define SFX_STOP            66

#define ANIM_STAND          0
#define ANIM_WAIT           1
#define ANIM_WALK           2
#define ANIM_RUN            3
#define ANIM_BRAKE          4
#define ANIM_UP             5
#define ANIM_CROUNCH        6
#define ANIM_ROLL           7


#define MAX_SPEED_DEFAULT   FIX32(8L)
#define RUN_SPEED           FIX32(6L)
#define BRAKE_SPEED         FIX32(2L)
#define JUMP_SPEED_DEFAULT  FIX32(7.8L)
#define GRAVITY_DEFAULT     FIX32(0.32)
#define ACCEL               FIX32(0.1)
#define DE_ACCEL            FIX32(0.15)

#define MAP_WIDTH           10240
#define MAP_HEIGHT          1280

#define MIN_POSX            FIX32(10L)
#define MAX_POSX            FIX32(MAP_WIDTH - 100)
#define MAX_POSY            FIX32(MAP_HEIGHT - 356)


// forward
static void handleInput();
static void joyEvent(u16 joy, u16 changed, u16 state);

static void setSpritePosition(Sprite* sprite, s16 posX, s16 posY);

static void updatePhysic();
static void updateAnim();

static void updateCameraPosition();
static void setCameraPosition(s16 x, s16 y);

// player (sonic) sprite
Sprite* player;
// enemies sprites
Sprite* enemies[2];

// maps (BGA and BGB)
Map *bgb;
Map *bga;

// absolute camera position (pixel)
s16 camPosX;
s16 camPosY;

// physic variables
fix32 maxSpeed;
fix32 jumpSpeed;
fix32 gravity;

// position and movement variables
fix32 posX;
fix32 posY;
fix32 movX;
fix32 movY;
s16 xOrder;
s16 yOrder;

// enemies positions and move direction
fix32 enemiesPosX[2];
fix32 enemiesPosY[2];
s16 enemiesXOrder[2];

// animation index table for enemies (static VRAM loading)
u16** sprTileIndexes[2];
// BG start tile index
u16 bgBaseTileIndex[2];

int main(u16 hard)
{
    u16 palette[64];
    u16 ind;

    // initialization
    VDP_setScreenWidth320();

    // init SFX
    XGM_setPCM(SFX_JUMP, sonic_jump_sfx, sizeof(sonic_jump_sfx));
    XGM_setPCM(SFX_ROLL, sonic_roll_sfx, sizeof(sonic_roll_sfx));
    XGM_setPCM(SFX_STOP, sonic_stop_sfx, sizeof(sonic_stop_sfx));
    // start music
    XGM_startPlay(sonic_music);

    // init sprite engine with default parameters
    SPR_init();

    // set all palette to black
    VDP_setPaletteColors(0, (u16*) palette_black, 64);

    // load background tilesets in VRAM
    ind = TILE_USERINDEX;
    bgBaseTileIndex[0] = ind;
    VDP_loadTileSet(&bga_tileset, ind, DMA);
    ind += bga_tileset.numTile;
    bgBaseTileIndex[1] = ind;
    VDP_loadTileSet(&bgb_tileset, ind, DMA);
    ind += bgb_tileset.numTile;

    // camera position (force refresh)
    camPosX = -1;
    camPosY = -1;

    // default speeds
    maxSpeed = MAX_SPEED_DEFAULT;
    jumpSpeed = JUMP_SPEED_DEFAULT;
    gravity = GRAVITY_DEFAULT;

    // set main sprite position (camera position may be adjusted depending it)
    posX = FIX32(48L);
    posY = MAX_POSY;
    movX = FIX32(0);
    movY = FIX32(0);
    xOrder = 0;
    yOrder = 0;

    // enemies position
    enemiesPosX[0] = FIX32(1000L);
    enemiesPosY[0] = MAX_POSY - FIX32(100);
    enemiesPosX[1] = FIX32(128L);
    enemiesPosY[1] = MAX_POSY + FIX32(5);
    enemiesXOrder[0] = -1;
    enemiesXOrder[1] = 1;

    // init backgrounds
    bga = MAP_create(&bga_map, BG_A, TILE_ATTR_FULL(0, FALSE, FALSE, FALSE, bgBaseTileIndex[0]));
    bgb = MAP_create(&bgb_map, BG_B, TILE_ATTR_FULL(0, FALSE, FALSE, FALSE, bgBaseTileIndex[1]));

    // init scrolling
    updateCameraPosition();

    // update camera position
    SYS_doVBlankProcess();

    // init sonic sprite
    player = SPR_addSprite(&sonic_sprite, fix32ToInt(posX) - camPosX, fix32ToInt(posY) - camPosY, TILE_ATTR(PAL0, TRUE, FALSE, FALSE));
    // init enemies sprites
    enemies[0] = SPR_addSprite(&enemy01_sprite, fix32ToInt(enemiesPosX[0]) - camPosX, fix32ToInt(enemiesPosY[0]) - camPosY, TILE_ATTR(PAL0, TRUE, FALSE, FALSE));
    enemies[1] = SPR_addSprite(&enemy02_sprite, fix32ToInt(enemiesPosX[1]) - camPosX, fix32ToInt(enemiesPosY[1]) - camPosY, TILE_ATTR(PAL0, TRUE, FALSE, FALSE));

    SPR_update();

    memcpy(&palette[0], palette_all.data, 64 * 2);

    // fade in
    PAL_fadeIn(0, (4 * 16) - 1, palette, 20, FALSE);

    JOY_setEventHandler(joyEvent);

    while(TRUE)
    {
        handleInput();

        // update internal sprite position
        updatePhysic();
        updateAnim();

        // update sprites
        SPR_update();

        // sync frame and do vblank process
        SYS_doVBlankProcess();


    }

    // release maps
    MEM_free(bga);
    MEM_free(bgb);

    return 0;
}


static void updatePhysic()
{
    u16 i;

    // sonic physic
    if (xOrder > 0)
    {
        movX += ACCEL;
        // going opposite side, quick breaking
        if (movX < 0) movX += ACCEL;

        if (movX >= maxSpeed) movX = maxSpeed;
    }
    else if (xOrder < 0)
    {
        movX -= ACCEL;
        // going opposite side, quick breaking
        if (movX > 0) movX -= ACCEL;

        if (movX <= -maxSpeed) movX = -maxSpeed;
    }
    else
    {
        if ((movX < FIX32(0.1)) && (movX > FIX32(-0.1)))
            movX = 0;
        else if ((movX < FIX32(0.3)) && (movX > FIX32(-0.3)))
            movX -= movX >> 2;
        else if ((movX < FIX32(1)) && (movX > FIX32(-1)))
            movX -= movX >> 3;
        else
            movX -= movX >> 4;
    }

    posX += movX;
    posY += movY;

    if (movY)
    {
        if (posY > MAX_POSY)
        {
            posY = MAX_POSY;
            movY = 0;
        }
        else movY += gravity;
    }

    if (posX >= MAX_POSX)
    {
        posX = MAX_POSX;
        movX = 0;
    }
    else if (posX <= MIN_POSX)
    {
        posX = MIN_POSX;
        movX = 0;
    }

    // enemies physic
    if (enemiesXOrder[0] > 0) enemiesPosX[0] += FIX32(1.5);
    else enemiesPosX[0] -= FIX32(1.5);
    if (enemiesXOrder[1] > 0) enemiesPosX[1] += FIX32(0.7);
    else enemiesPosX[1] -= FIX32(0.7);
    for(i = 0; i < 2; i++)
    {
        if ((enemiesPosX[i] >= MAX_POSX) || (enemiesPosX[i] <= MIN_POSX))
            enemiesXOrder[i] = -enemiesXOrder[i];
    }

    // update camera position (*after* player sprite position has been updated)
    updateCameraPosition();

    // set sprites position
    setSpritePosition(player, fix32ToInt(posX) - camPosX, fix32ToInt(posY) - camPosY);
    setSpritePosition(enemies[0], fix32ToInt(enemiesPosX[0]) - camPosX, fix32ToInt(enemiesPosY[0]) - camPosY);
    setSpritePosition(enemies[1], fix32ToInt(enemiesPosX[1]) - camPosX, fix32ToInt(enemiesPosY[1]) - camPosY);
}

static void setSpritePosition(Sprite* sprite, s16 x, s16 y)
{
    // clip out of screen sprites
    if ((x < -100) || (x > 320) || (y < -100) || (y > 240)) SPR_setVisibility(sprite, HIDDEN);
    else
    {
        SPR_setVisibility(sprite, VISIBLE);
        SPR_setPosition(sprite, x, y);
    }
}

static void updateAnim()
{
    // jumping
    if (movY) SPR_setAnim(player, ANIM_ROLL);
    else
    {
        if (((movX >= BRAKE_SPEED) && (xOrder < 0)) || ((movX <= -BRAKE_SPEED) && (xOrder > 0)))
        {
            if (player->animInd != ANIM_BRAKE)
            {
                SND_startPlayPCM_XGM(SFX_STOP, 1, SOUND_PCM_CH2);
                SPR_setAnim(player, ANIM_BRAKE);
            }
        }
        else if ((movX >= RUN_SPEED) || (movX <= -RUN_SPEED))
            SPR_setAnim(player, ANIM_RUN);
        else if (movX != 0)
            SPR_setAnim(player, ANIM_WALK);
        else
        {
            if (yOrder < 0)
                SPR_setAnim(player, ANIM_UP);
            else if (yOrder > 0)
                SPR_setAnim(player, ANIM_CROUNCH);
            else
                SPR_setAnim(player, ANIM_STAND);
        }
    }

    if (movX > 0) SPR_setHFlip(player, FALSE);
    else if (movX < 0) SPR_setHFlip(player, TRUE);

    // enemies
    if (enemiesXOrder[0] > 0) SPR_setHFlip(enemies[0], TRUE);
    else SPR_setHFlip(enemies[0], FALSE);

}

static void updateCameraPosition()
{
    // get player position (pixel)
    s16 px = fix32ToInt(posX);
    s16 py = fix32ToInt(posY);
    // current sprite position on screen
    s16 px_scr = px - camPosX;
    s16 py_scr = py - camPosY;

    s16 npx_cam, npy_cam;

    // adjust new camera position
    //PIGSY NOTE: adjusting the numbers in this little section will change how far to the edges of the screen the sprite
    //can get before the camera starts moving
    if (px_scr > 200) npx_cam = px - 200;
    else if (px_scr < 100) npx_cam = px - 100;
    else npx_cam = camPosX;
    if (py_scr > 140) npy_cam = py - 140;
    else if (py_scr < 60) npy_cam = py - 60;
    else npy_cam = camPosY;

    // clip camera position
    if (npx_cam < 0) npx_cam = 0;
    else if (npx_cam > (MAP_WIDTH - 320)) npx_cam = (MAP_WIDTH - 320);
    if (npy_cam < 0) npy_cam = 0;
    else if (npy_cam > (MAP_HEIGHT - 224)) npy_cam = (MAP_HEIGHT - 224);

    // set new camera position
    setCameraPosition(npx_cam, npy_cam);
}

static void setCameraPosition(s16 x, s16 y)
{
    if ((x != camPosX) || (y != camPosY))
    {
        camPosX = x;
        camPosY = y;
        
        // scroll maps
        MAP_scrollTo(bga, x, y);
        // scrolling is slower on BGB
        MAP_scrollTo(bgb, x >> 3, y >> 5);
    }
}

static void handleInput()
{
    u16 value = JOY_readJoypad(JOY_1);

    if (value & BUTTON_UP) yOrder = -1;
    else if (value & BUTTON_DOWN) yOrder = +1;
    else yOrder = 0;

    if (value & BUTTON_LEFT) xOrder = -1;
    else if (value & BUTTON_RIGHT) xOrder = +1;
    else xOrder = 0;
}

static void joyEvent(u16 joy, u16 changed, u16 state)
{
    if (changed & state & (BUTTON_A | BUTTON_B | BUTTON_C))
    {
        if (movY == 0)
        {
            movY = -jumpSpeed;
            SND_startPlayPCM_XGM(SFX_JUMP, 1, SOUND_PCM_CH2);
        }
    }
}
