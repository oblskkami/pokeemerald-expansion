#include "global.h"
#include "naming_screen.h"
#include "malloc.h"
#include "palette.h"
#include "task.h"
#include "sprite.h"
#include "string_util.h"
#include "international_string_util.h"
#include "window.h"
#include "bg.h"
#include "gpu_regs.h"
#include "pokemon.h"
#include "field_specials.h"
#include "field_player_avatar.h"
#include "event_object_movement.h"
#include "event_data.h"
#include "constants/songs.h"
#include "pokemon_storage_system.h"
#include "graphics.h"
#include "sound.h"
#include "field_effect.h"
#include "pokemon_icon.h"
#include "data.h"
#include "strings.h"
#include "menu.h"
#include "text_window.h"
#include "overworld.h"
#include "walda_phrase.h"
#include "main.h"
#include "decompress.h"
#include "constants/event_objects.h"
#include "constants/rgb.h"

enum {
    STATE_FADE_IN,
    STATE_WAIT_FADE_IN,
    STATE_HANDLE_INPUT,
    STATE_PRESSED_OK,
    STATE_FADE_OUT,
    STATE_EXIT,
};

enum {
    WIN_TITLE,
    WIN_TEXT_ENTRY,
    WIN_PINYIN,
    WIN_KEYBOARD,
    WIN_CANDIDATES,
    WIN_CONTROLS,
    WIN_COUNT,
};

enum {
    MODE_PINYIN,
    MODE_DIRECT,
};

enum {
    MODE_BUTTON_PINYIN,
    MODE_BUTTON_ABC,
    MODE_BUTTON_SYMBOLS,
    MODE_BUTTON_COUNT,
};

enum {
    REGION_KEYS,
    REGION_CANDIDATES,
    REGION_BACK,
    REGION_MODE,
    REGION_OK,
};

enum {
    CURSOR_SPRITE_SMALL,
    CURSOR_SPRITE_BUTTON,
    CURSOR_SPRITE_LONG_BUTTON,
    CURSOR_SPRITE_MODE_STATUS,
    CURSOR_SPRITE_COUNT,
};

#define PINYIN_KEYS_PER_ROW  10
#define PINYIN_KEY_ROWS       3
#define PINYIN_KEY_COUNT     30
#define CANDIDATE_COLS       13
#define CANDIDATE_ROWS        1
#define CANDIDATES_PER_PAGE  (CANDIDATE_COLS * CANDIDATE_ROWS)
#define DIRECT_PAGE_UPPER     0
#define DIRECT_PAGE_LOWER     1
#define DIRECT_PAGE_SYMBOLS   2
#define DIRECT_PAGE_COUNT     3
#define PINYIN_BUFFER_LENGTH  8
#define TEXT_BUFFER_LENGTH   ((WALDA_PHRASE_LENGTH * 2) + 1)
#define PALTAG_PC_ICON       0
#define GFXTAG_PINYIN_CURSOR_SMALL        0x7000
#define GFXTAG_PINYIN_CURSOR_BUTTON       0x7001
#define GFXTAG_PINYIN_CURSOR_LONG_BUTTON  0x7002
#define PALTAG_PINYIN_CURSOR_SMALL        0x7000
#define PALTAG_PINYIN_CURSOR_BUTTON       0x7001
#define TEXT_ENTRY_WIDTH    136
#define TEXT_ENTRY_WINDOW_LEFT 64
#define TEXT_ENTRY_SLOT_WIDTH 8
#define TEXT_ENTRY_TEXT_Y 0
#define TEXT_ENTRY_UNDERLINE_Y 15
#define TEXT_ENTRY_UNDERLINE_X_OFFSET 1
#define TEXT_ENTRY_UNDERLINE_WIDTH 6
#define TEXT_ENTRY_ACTIVE_LINE_Y 14
#define TEXT_ENTRY_ANIM_DELAY 8
#define KEYBOARD_KEY_STEP_X  15
#define KEYBOARD_KEY_STEP_Y  19
#define PINYIN_CANDIDATE_LEFT 19
#define PINYIN_CANDIDATE_TOP  65
#define PINYIN_KEYBOARD_LEFT  19
#define PINYIN_KEYBOARD_TOP   84
#define PINYIN_BUTTON_TOP    141
#define PINYIN_MODE_BUTTON_PY_LEFT      19
#define PINYIN_MODE_BUTTON_ABC_LEFT     43
#define PINYIN_MODE_BUTTON_SYMBOL_LEFT  67
#define PINYIN_DELETE_BUTTON_LEFT      106
#define PINYIN_OK_BUTTON_LEFT          160
#define PINYIN_CONTROLS_LEFT            16
#define PINYIN_MODE_BUTTON_WIDTH        23
#define PINYIN_ACTION_BUTTON_WIDTH      53
#define PINYIN_CONTROL_TEXT_Y            7
#define PINYIN_TOP_TEXT_WIDTH          240
#define TITLE_TEXT_LEFT                  0
#define TITLE_TEXT_TOP                   0
#define PINYIN_BUFFER_TEXT_X             4
#define PINYIN_BUFFER_TEXT_Y             0
#define PINYIN_CURSOR_SMALL_HALF_WIDTH   16
#define PINYIN_CURSOR_SMALL_HALF_HEIGHT  16
#define PINYIN_CURSOR_LONG_HALF_WIDTH    32
#define PINYIN_CURSOR_BUTTON_HALF_WIDTH  16
#define PINYIN_CURSOR_BUTTON_HALF_HEIGHT 16
#define PINYIN_CURSOR_BLINK_DELAY        16
#define PINYIN_DELETE_CURSOR_FLASH_FRAMES (PINYIN_CURSOR_BLINK_DELAY * 2)
#define INPUT_ICON_X         29
#define INPUT_ICON_Y         24
#define INPUT_PC_ICON_Y      28
#define INPUT_MON_ICON_Y     27

struct NamingScreenTemplate
{
    u8 copyExistingString;
    u8 maxChars;
    u8 iconFunction;
    u8 addGenderIcon;
    const u8 *title;
};

struct NamingScreenData
{
    u16 bg0TilemapBuffer[0x800 / 2];
    u16 bg1TilemapBuffer[0x800 / 2];
    u8 state;
    u8 windows[WIN_COUNT];
    const struct NamingScreenTemplate *template;
    u8 templateNum;
    u8 *destBuffer;
    enum Species monSpecies;
    u16 monGender;
    u32 monPersonality;
    MainCallback returnCallback;
    u8 textBuffer[TEXT_BUFFER_LENGTH];
    u8 textByteLength;
    u8 textCharCount;
    u8 pinyinAscii[PINYIN_BUFFER_LENGTH + 1];
    u8 pinyinDisplay[PINYIN_BUFFER_LENGTH + 1];
    u8 pinyinLength;
    u8 mode;
    u8 directPage;
    u8 modeCursor;
    u8 cursorRegion;
    u8 cursorX;
    u8 cursorY;
    u8 candidatePage;
    const u8 *candidates;
    u8 candidateCount;
    u8 textEntryAnimDelay;
    u8 textEntryAnimFrame;
    u8 deleteCursorFlashTimer;
    u8 cursorSpriteIds[CURSOR_SPRITE_COUNT];
};

EWRAM_DATA static struct NamingScreenData *sNamingScreen = NULL;

static const u8 sPCIconOff_Gfx[] = INCGFX_U8("graphics/naming_screen/pc_icon_off.png", ".4bpp");
static const u8 sPCIconOn_Gfx[] = INCGFX_U8("graphics/naming_screen/pc_icon_on.png", ".4bpp");
static const u16 sRival_Gfx[] = INCGFX_U16("graphics/naming_screen/rival.png", ".4bpp");
static const u16 sRival_Pal[] = INCGFX_U16("graphics/naming_screen/rival.pal", ".gbapal");
static const u8 sPinyinCursorSmall_Gfx[] = INCGFX_U8("graphics/pinyin/arrow_1.png", ".4bpp");
static const u8 sPinyinCursorButton_Gfx[] = INCGFX_U8("graphics/pinyin/arrow_2.png", ".4bpp");
static const u8 sPinyinCursorLongButton_Gfx[] = INCGFX_U8("graphics/pinyin/arrow_3.png", ".4bpp");
static const u16 sPinyinCursorSmall_Pal[] = INCGFX_U16("graphics/pinyin/arrow_1.png", ".gbapal");
static const u16 sPinyinCursorButton_Pal[] = INCGFX_U16("graphics/pinyin/arrow_2.png", ".gbapal");

static const u8 *const sTransferredToPCMessages[] =
{
    gText_PkmnTransferredSomeonesPC,
    gText_PkmnTransferredLanettesPC,
    gText_PkmnTransferredSomeonesPCBoxFull,
    gText_PkmnTransferredLanettesPCBoxFull
};

static const u8 sText_RivalsName[] = _("劲敌的名字?");
static const u8 sText_PlayerNamePrompt[] = _("你的名字？");
static const u8 sText_MonNamePrompt[] = _("的名字？");
static const u8 sText_ModePinyin[] = _("拼音");
static const u8 sText_ModeDirectUpper[] = _("ABC");
static const u8 sText_ModeDirectLower[] = _("abc");
static const u8 sText_ModeDirectSymbols[] = _("1/?");
static const u8 sText_DirectHint[] = _("L/R 翻页");
static const u8 sText_Backspace[] = _("删除文字");
static const u8 sText_Ok[] = _("确认");

static const u8 sPinyinKeysAscii[PINYIN_KEY_COUNT] =
{
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', 'v',
    'z', 'x', 'c', 'b', 'n', 'm', 0, 0, 0, 0,
};

static const u8 sPinyinKeysDisplay[PINYIN_KEY_COUNT] =
{
    CHAR_q, CHAR_w, CHAR_e, CHAR_r, CHAR_t, CHAR_y, CHAR_u, CHAR_i, CHAR_o, CHAR_p,
    CHAR_a, CHAR_s, CHAR_d, CHAR_f, CHAR_g, CHAR_h, CHAR_j, CHAR_k, CHAR_l, CHAR_v,
    CHAR_z, CHAR_x, CHAR_c, CHAR_b, CHAR_n, CHAR_m, EOS, EOS, EOS, EOS,
};

static const u8 sDirectKeyChars[DIRECT_PAGE_COUNT][PINYIN_KEY_COUNT] =
{
    {
        CHAR_A, CHAR_B, CHAR_C, CHAR_D, CHAR_E, CHAR_F, CHAR_G, CHAR_H, CHAR_I, CHAR_J,
        CHAR_K, CHAR_L, CHAR_M, CHAR_N, CHAR_O, CHAR_P, CHAR_Q, CHAR_R, CHAR_S, CHAR_T,
        CHAR_U, CHAR_V, CHAR_W, CHAR_X, CHAR_Y, CHAR_Z, EOS, EOS, EOS, EOS,
    },
    {
        CHAR_a, CHAR_b, CHAR_c, CHAR_d, CHAR_e, CHAR_f, CHAR_g, CHAR_h, CHAR_i, CHAR_j,
        CHAR_k, CHAR_l, CHAR_m, CHAR_n, CHAR_o, CHAR_p, CHAR_q, CHAR_r, CHAR_s, CHAR_t,
        CHAR_u, CHAR_v, CHAR_w, CHAR_x, CHAR_y, CHAR_z, EOS, EOS, EOS, EOS,
    },
    {
        CHAR_0, CHAR_1, CHAR_2, CHAR_3, CHAR_4, CHAR_5, CHAR_6, CHAR_7, CHAR_8, CHAR_9,
        CHAR_EXCL_MARK, CHAR_QUESTION_MARK, CHAR_PERIOD, CHAR_HYPHEN, CHAR_COMMA, CHAR_COLON, CHAR_SEMICOLON, CHAR_PLUS, CHAR_EQUALS, CHAR_SLASH,
        CHAR_AMPERSAND, CHAR_LEFT_PAREN, CHAR_RIGHT_PAREN, CHAR_DBL_QUOTE_LEFT, CHAR_DBL_QUOTE_RIGHT, CHAR_SGL_QUOTE_LEFT, CHAR_SGL_QUOTE_RIGHT, CHAR_MALE, CHAR_FEMALE, CHAR_SPACE,
    },
};

static const struct BgTemplate sBgTemplates[] =
{
    {
        .bg = 0,
        .charBaseIndex = 2,
        .mapBaseIndex = 30,
        .priority = 0
    },
    {
        .bg = 1,
        .charBaseIndex = 0,
        .mapBaseIndex = 31,
        .priority = 3
    },
};

static const struct WindowTemplate sWindowTemplates[WIN_COUNT + 1] =
{
    [WIN_TITLE] = {
        .bg = 0,
        .tilemapLeft = 0,
        .tilemapTop = 0,
        .width = 30,
        .height = 2,
        .paletteNum = 14,
        .baseBlock = 0x001
    },
    [WIN_TEXT_ENTRY] = {
        .bg = 0,
        .tilemapLeft = 8,
        .tilemapTop = 3,
        .width = 17,
        .height = 2,
        .paletteNum = 14,
        .baseBlock = 0x03D
    },
    [WIN_PINYIN] = {
        .bg = 0,
        .tilemapLeft = 2,
        .tilemapTop = 6,
        .width = 8,
        .height = 2,
        .paletteNum = 14,
        .baseBlock = 0x05F
    },
    [WIN_KEYBOARD] = {
        .bg = 0,
        .tilemapLeft = 2,
        .tilemapTop = 10,
        .width = 26,
        .height = 7,
        .paletteNum = 14,
        .baseBlock = 0x0A3
    },
    [WIN_CANDIDATES] = {
        .bg = 0,
        .tilemapLeft = 2,
        .tilemapTop = 8,
        .width = 26,
        .height = 2,
        .paletteNum = 14,
        .baseBlock = 0x06F
    },
    [WIN_CONTROLS] = {
        .bg = 0,
        .tilemapLeft = 2,
        .tilemapTop = 17,
        .width = 26,
        .height = 3,
        .paletteNum = 14,
        .baseBlock = 0x159
    },
    DUMMY_WIN_TEMPLATE
};

static const u8 sTextColors[] = {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_WHITE, TEXT_COLOR_DARK_GRAY};
static const u8 sMutedTextColors[] = {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_LIGHT_GRAY, TEXT_COLOR_DARK_GRAY};
static const u8 sGenderColors[2][3] =
{
    {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_LIGHT_BLUE, TEXT_COLOR_BLUE},
    {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_LIGHT_RED, TEXT_COLOR_RED}
};

static void SpriteCB_PinyinCursor(struct Sprite *sprite);

static const struct SpriteSheet sPinyinCursorSpriteSheets[] =
{
    {sPinyinCursorSmall_Gfx,      0x200, GFXTAG_PINYIN_CURSOR_SMALL},
    {sPinyinCursorButton_Gfx,     0x200, GFXTAG_PINYIN_CURSOR_BUTTON},
    {sPinyinCursorLongButton_Gfx, 0x400, GFXTAG_PINYIN_CURSOR_LONG_BUTTON},
    {}
};

static const struct SpritePalette sPinyinCursorSpritePalettes[] =
{
    {sPinyinCursorSmall_Pal,  PALTAG_PINYIN_CURSOR_SMALL},
    {sPinyinCursorButton_Pal, PALTAG_PINYIN_CURSOR_BUTTON},
    {}
};

static const struct OamData sOam_32x32 =
{
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_NORMAL,
    .bpp = ST_OAM_4BPP,
    .shape = SPRITE_SHAPE(32x32),
    .size = SPRITE_SIZE(32x32),
};

static const struct OamData sOam_64x32 =
{
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_NORMAL,
    .bpp = ST_OAM_4BPP,
    .shape = SPRITE_SHAPE(64x32),
    .size = SPRITE_SIZE(64x32),
};

static const struct SpriteTemplate sSpriteTemplate_PinyinCursorSmall =
{
    .tileTag = GFXTAG_PINYIN_CURSOR_SMALL,
    .paletteTag = PALTAG_PINYIN_CURSOR_SMALL,
    .oam = &sOam_32x32,
    .anims = gDummySpriteAnimTable,
    .images = NULL,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = SpriteCB_PinyinCursor
};

static const struct SpriteTemplate sSpriteTemplate_PinyinCursorButton =
{
    .tileTag = GFXTAG_PINYIN_CURSOR_BUTTON,
    .paletteTag = PALTAG_PINYIN_CURSOR_BUTTON,
    .oam = &sOam_32x32,
    .anims = gDummySpriteAnimTable,
    .images = NULL,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = SpriteCB_PinyinCursor
};

static const struct SpriteTemplate sSpriteTemplate_PinyinCursorLongButton =
{
    .tileTag = GFXTAG_PINYIN_CURSOR_LONG_BUTTON,
    .paletteTag = PALTAG_PINYIN_CURSOR_BUTTON,
    .oam = &sOam_64x32,
    .anims = gDummySpriteAnimTable,
    .images = NULL,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = SpriteCB_PinyinCursor
};

static const struct NamingScreenTemplate *const sNamingScreenTemplates[];

#include "data/pinyin_table.h"

static void CB2_LoadNamingScreen(void);
static void CB2_NamingScreen(void);
static void VBlankCB_NamingScreen(void);
static void NamingScreen_Init(void);
static void NamingScreen_InitBGs(void);
static void LoadGfx(void);
static void LoadPalettes(void);
static void CreateCursorSprites(void);
static void UpdateCursorSprites(void);
static u8 GetModeCursorX(u8 modeButton);
static const u8 *GetModeButtonLabel(u8 modeButton);
static u8 GetCurrentModeButton(void);
static void SetInputMode(u8 mode, u8 directPage);
static void SelectModeButton(void);
static void CycleInputMode(void);
static u8 GetCandidatesOnCurrentPage(void);
static u8 GetCandidatePageCount(void);
static bool8 HasMultipleCandidatePages(void);
static bool8 ChangeCandidatePage(s8 delta, bool8 keepCursor);
static void ClampCandidateCursor(void);
static void CreateNamingScreenTask(void);
static void Task_NamingScreen(u8 taskId);
static void DrawScreen(void);
static void DrawTitle(void);
static void DrawTextEntry(void);
static void DrawPinyinBuffer(void);
static void DrawKeyboard(void);
static void DrawCandidates(void);
static void DrawControls(void);
static void DrawControlText(const u8 *text, u8 left, u8 width);
static void ResetCandidateSelection(void);
static void UpdateCandidates(void);
static const struct PinyinCandidateGroup *FindCandidateGroup(const u8 *pinyin);
static bool8 PinyinAsciiEquals(const u8 *left, const u8 *right);
static void HandleInput(void);
static void MoveCursor(s8 dx, s8 dy);
static void ActivateSelection(void);
static bool8 DeleteInput(void);
static bool8 DeletePinyinCharacter(void);
static void StartDeleteCursorFlash(void);
static void UpdateDeleteCursorFlash(void);
static bool8 AddDirectCharacter(u8 ch);
static bool8 AddCandidateCharacter(const u8 *ch);
static bool8 AppendTextChar(const u8 *ch);
static bool8 IsKeyboardIndexValid(u8 index);
static u8 GetKeyboardRowKeyCount(u8 row);
static void ClampKeyboardCursor(void);
static void SaveInputText(void);
static u8 GetEncodedCharByteLength(const u8 *str);
static u8 CountEncodedChars(const u8 *str);
static const u8 *GetEncodedCharAt(const u8 *str, u8 index);
static u8 CountTextChars(const u8 *str, u8 byteLimit);
static void CopyExistingString(void);
static void ClearPinyinBuffer(void);
static u8 GetKeyboardIndexAtCursor(void);
static u8 GetCandidateIndexAtCursor(void);
static void CreateInputTargetIcon(void);
static void DisplaySentToPCMessage(void);
static void NamingScreen_ShowBgs(void);

void DoNamingScreen(u8 templateNum, u8 *destBuffer, u16 monSpeciesOrPlayerGender, u16 monGender, u32 monPersonality, MainCallback returnCallback)
{
    sNamingScreen = AllocZeroed(sizeof(struct NamingScreenData));
    if (sNamingScreen == NULL)
    {
        SetMainCallback2(returnCallback);
    }
    else
    {
        sNamingScreen->templateNum = templateNum;
        sNamingScreen->monSpecies = monSpeciesOrPlayerGender;
        sNamingScreen->monGender = monGender;
        sNamingScreen->monPersonality = monPersonality;
        sNamingScreen->destBuffer = destBuffer;
        sNamingScreen->returnCallback = returnCallback;

        if (templateNum == NAMING_SCREEN_PLAYER)
            StartTimer1();

        SetMainCallback2(CB2_LoadNamingScreen);
    }
}

static void CB2_LoadNamingScreen(void)
{
    switch (gMain.state)
    {
    case 0:
        SetVBlankCallback(NULL);
        SetHBlankCallback(NULL);
        NamingScreen_Init();
        gMain.state++;
        break;
    case 1:
        NamingScreen_InitBGs();
        gMain.state++;
        break;
    case 2:
        ResetPaletteFade();
        gMain.state++;
        break;
    case 3:
        ResetSpriteData();
        FreeAllSpritePalettes();
        gMain.state++;
        break;
    case 4:
        ResetTasks();
        gMain.state++;
        break;
    case 5:
        LoadPalettes();
        gMain.state++;
        break;
    case 6:
        LoadGfx();
        gMain.state++;
        break;
    case 7:
        CreateCursorSprites();
        CreateInputTargetIcon();
        NamingScreen_ShowBgs();
        DrawScreen();
        CopyBgTilemapBufferToVram(0);
        CopyBgTilemapBufferToVram(1);
        UpdatePaletteFade();
        gMain.state++;
        break;
    default:
        CreateNamingScreenTask();
        break;
    }
}

static void NamingScreen_Init(void)
{
    sNamingScreen->state = STATE_FADE_IN;
    sNamingScreen->template = sNamingScreenTemplates[sNamingScreen->templateNum];
    sNamingScreen->mode = MODE_PINYIN;
    sNamingScreen->directPage = 0;
    sNamingScreen->modeCursor = MODE_BUTTON_PINYIN;
    sNamingScreen->cursorRegion = REGION_KEYS;
    sNamingScreen->cursorX = 0;
    sNamingScreen->cursorY = 0;
    sNamingScreen->textBuffer[0] = EOS;
    ClearPinyinBuffer();
    if (sNamingScreen->template->copyExistingString)
        CopyExistingString();
    UpdateCandidates();
}

static void NamingScreen_InitBGs(void)
{
    u8 i;

    DmaClearLarge16(3, (void *)VRAM, VRAM_SIZE, 0x1000);
    DmaClear32(3, (void *)OAM, OAM_SIZE);
    DmaClear16(3, (void *)PLTT, PLTT_SIZE);

    SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_MODE_0);
    ResetBgsAndClearDma3BusyFlags(0);
    InitBgsFromTemplates(0, sBgTemplates, ARRAY_COUNT(sBgTemplates));

    ChangeBgX(0, 0, BG_COORD_SET);
    ChangeBgY(0, 0, BG_COORD_SET);
    ChangeBgX(1, 0, BG_COORD_SET);
    ChangeBgY(1, 0, BG_COORD_SET);

    InitStandardTextBoxWindows();
    InitTextBoxGfxAndPrinters();

    for (i = 0; i < WIN_COUNT; i++)
        sNamingScreen->windows[i] = AddWindow(&sWindowTemplates[i]);

    SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_OBJ_1D_MAP | DISPCNT_OBJ_ON);
    SetBgTilemapBuffer(0, sNamingScreen->bg0TilemapBuffer);
    SetBgTilemapBuffer(1, sNamingScreen->bg1TilemapBuffer);
    FillBgTilemapBufferRect_Palette0(0, 0, 0, 0, 0x20, 0x20);
    FillBgTilemapBufferRect_Palette0(1, 0, 0, 0, 0x20, 0x20);
}

static void LoadGfx(void)
{
    DecompressDataWithHeaderVram(gPinyinNamingScreenBackground_Gfx, (void *)BG_CHAR_ADDR(0));
    CopyToBgTilemapBuffer(1, gPinyinNamingScreenBackground_Tilemap, 0, 0);
    LoadSpriteSheets(sPinyinCursorSpriteSheets);
}

static void LoadPalettes(void)
{
    LoadPalette(gPinyinNamingScreenBackground_Pal, BG_PLTT_ID(0), PLTT_SIZE_4BPP);
    LoadPalette(GetTextWindowPalette(2), BG_PLTT_ID(14), PLTT_SIZE_4BPP);
    LoadSpritePalette(&(const struct SpritePalette){gNamingScreenMenu_Pal[0], PALTAG_PC_ICON});
    LoadSpritePalettes(sPinyinCursorSpritePalettes);
}

static void CreateCursorSprites(void)
{
    u8 i;

    sNamingScreen->cursorSpriteIds[CURSOR_SPRITE_SMALL] = CreateSprite(&sSpriteTemplate_PinyinCursorSmall, 0, 0, 0);
    sNamingScreen->cursorSpriteIds[CURSOR_SPRITE_BUTTON] = CreateSprite(&sSpriteTemplate_PinyinCursorButton, 0, 0, 0);
    sNamingScreen->cursorSpriteIds[CURSOR_SPRITE_LONG_BUTTON] = CreateSprite(&sSpriteTemplate_PinyinCursorLongButton, 0, 0, 0);
    sNamingScreen->cursorSpriteIds[CURSOR_SPRITE_MODE_STATUS] = CreateSprite(&sSpriteTemplate_PinyinCursorButton, 0, 0, 0);

    for (i = 0; i < CURSOR_SPRITE_COUNT; i++)
    {
        if (sNamingScreen->cursorSpriteIds[i] != MAX_SPRITES)
        {
            gSprites[sNamingScreen->cursorSpriteIds[i]].oam.priority = 1;
            gSprites[sNamingScreen->cursorSpriteIds[i]].invisible = TRUE;
            gSprites[sNamingScreen->cursorSpriteIds[i]].data[0] = 0;
            gSprites[sNamingScreen->cursorSpriteIds[i]].data[1] = 0;
        }
    }

    if (sNamingScreen->cursorSpriteIds[CURSOR_SPRITE_MODE_STATUS] != MAX_SPRITES)
        gSprites[sNamingScreen->cursorSpriteIds[CURSOR_SPRITE_MODE_STATUS]].callback = SpriteCallbackDummy;

    UpdateCursorSprites();
}

static void SpriteCB_PinyinCursor(struct Sprite *sprite)
{
    if (!sprite->data[2])
    {
        sprite->invisible = TRUE;
        return;
    }

    if (++sprite->data[0] >= PINYIN_CURSOR_BLINK_DELAY)
    {
        sprite->data[0] = 0;
        sprite->data[1] ^= 1;
    }

    sprite->invisible = sprite->data[1];
}

static void UpdateCursorSprites(void)
{
    u8 spriteId;
    u8 i;
    s16 x;
    s16 y;

    for (i = 0; i < CURSOR_SPRITE_MODE_STATUS; i++)
    {
        spriteId = sNamingScreen->cursorSpriteIds[i];
        if (spriteId != MAX_SPRITES)
        {
            gSprites[spriteId].data[2] = 0;
            gSprites[spriteId].invisible = TRUE;
        }
    }

    spriteId = sNamingScreen->cursorSpriteIds[CURSOR_SPRITE_MODE_STATUS];
    if (spriteId != MAX_SPRITES)
    {
        gSprites[spriteId].x = GetModeCursorX(GetCurrentModeButton()) + PINYIN_CURSOR_BUTTON_HALF_WIDTH;
        gSprites[spriteId].y = PINYIN_BUTTON_TOP + PINYIN_CURSOR_BUTTON_HALF_HEIGHT;
        gSprites[spriteId].invisible = (sNamingScreen->cursorRegion == REGION_MODE
                                     && sNamingScreen->modeCursor == GetCurrentModeButton());
    }

    if (sNamingScreen->deleteCursorFlashTimer != 0)
    {
        spriteId = sNamingScreen->cursorSpriteIds[CURSOR_SPRITE_LONG_BUTTON];
        if (spriteId == MAX_SPRITES)
            return;

        gSprites[spriteId].x = PINYIN_DELETE_BUTTON_LEFT + PINYIN_CURSOR_LONG_HALF_WIDTH;
        gSprites[spriteId].y = PINYIN_BUTTON_TOP + PINYIN_CURSOR_BUTTON_HALF_HEIGHT;
        gSprites[spriteId].data[0] = 0;
        gSprites[spriteId].data[1] = 0;
        gSprites[spriteId].data[2] = 1;
        gSprites[spriteId].invisible = FALSE;
        return;
    }

    switch (sNamingScreen->cursorRegion)
    {
    case REGION_CANDIDATES:
        if (sNamingScreen->mode == MODE_DIRECT || GetCandidatesOnCurrentPage() == 0)
            return;
        spriteId = sNamingScreen->cursorSpriteIds[CURSOR_SPRITE_SMALL];
        if (spriteId == MAX_SPRITES)
            return;
        x = PINYIN_CANDIDATE_LEFT + sNamingScreen->cursorX * KEYBOARD_KEY_STEP_X + PINYIN_CURSOR_SMALL_HALF_WIDTH;
        y = PINYIN_CANDIDATE_TOP + sNamingScreen->cursorY * KEYBOARD_KEY_STEP_Y + PINYIN_CURSOR_SMALL_HALF_HEIGHT;
        break;
    case REGION_KEYS:
        spriteId = sNamingScreen->cursorSpriteIds[CURSOR_SPRITE_SMALL];
        if (spriteId == MAX_SPRITES)
            return;
        x = PINYIN_KEYBOARD_LEFT + sNamingScreen->cursorX * KEYBOARD_KEY_STEP_X + PINYIN_CURSOR_SMALL_HALF_WIDTH;
        y = PINYIN_KEYBOARD_TOP + sNamingScreen->cursorY * KEYBOARD_KEY_STEP_Y + PINYIN_CURSOR_SMALL_HALF_HEIGHT;
        break;
    case REGION_MODE:
        spriteId = sNamingScreen->cursorSpriteIds[CURSOR_SPRITE_BUTTON];
        if (spriteId == MAX_SPRITES)
            return;
        x = GetModeCursorX(sNamingScreen->modeCursor) + PINYIN_CURSOR_BUTTON_HALF_WIDTH;
        y = PINYIN_BUTTON_TOP + PINYIN_CURSOR_BUTTON_HALF_HEIGHT;
        break;
    case REGION_BACK:
        spriteId = sNamingScreen->cursorSpriteIds[CURSOR_SPRITE_LONG_BUTTON];
        if (spriteId == MAX_SPRITES)
            return;
        x = PINYIN_DELETE_BUTTON_LEFT + PINYIN_CURSOR_LONG_HALF_WIDTH;
        y = PINYIN_BUTTON_TOP + PINYIN_CURSOR_BUTTON_HALF_HEIGHT;
        break;
    case REGION_OK:
    default:
        spriteId = sNamingScreen->cursorSpriteIds[CURSOR_SPRITE_LONG_BUTTON];
        if (spriteId == MAX_SPRITES)
            return;
        x = PINYIN_OK_BUTTON_LEFT + PINYIN_CURSOR_LONG_HALF_WIDTH;
        y = PINYIN_BUTTON_TOP + PINYIN_CURSOR_BUTTON_HALF_HEIGHT;
        break;
    }

    gSprites[spriteId].x = x;
    gSprites[spriteId].y = y;
    gSprites[spriteId].data[0] = 0;
    gSprites[spriteId].data[1] = 0;
    gSprites[spriteId].data[2] = 1;
    gSprites[spriteId].invisible = FALSE;
}

static u8 GetModeCursorX(u8 modeButton)
{
    switch (modeButton)
    {
    case MODE_BUTTON_PINYIN:
        return PINYIN_MODE_BUTTON_PY_LEFT;
    case MODE_BUTTON_SYMBOLS:
        return PINYIN_MODE_BUTTON_SYMBOL_LEFT;
    case MODE_BUTTON_ABC:
    default:
        return PINYIN_MODE_BUTTON_ABC_LEFT;
    }
}

static const u8 *GetModeButtonLabel(u8 modeButton)
{
    switch (modeButton)
    {
    case MODE_BUTTON_PINYIN:
        return sText_ModePinyin;
    case MODE_BUTTON_SYMBOLS:
        return sText_ModeDirectSymbols;
    case MODE_BUTTON_ABC:
    default:
        if (sNamingScreen->mode == MODE_DIRECT && sNamingScreen->directPage == DIRECT_PAGE_LOWER)
            return sText_ModeDirectLower;
        return sText_ModeDirectUpper;
    }
}

static u8 GetCurrentModeButton(void)
{
    if (sNamingScreen->mode == MODE_PINYIN)
        return MODE_BUTTON_PINYIN;
    if (sNamingScreen->directPage == DIRECT_PAGE_SYMBOLS)
        return MODE_BUTTON_SYMBOLS;
    return MODE_BUTTON_ABC;
}

static void SetInputMode(u8 mode, u8 directPage)
{
    if (sNamingScreen->mode == mode && sNamingScreen->directPage == directPage)
        return;

    sNamingScreen->mode = mode;
    sNamingScreen->directPage = directPage;
    sNamingScreen->modeCursor = GetCurrentModeButton();
    ClearPinyinBuffer();
    ResetCandidateSelection();
    ClampKeyboardCursor();
    PlaySE(SE_WIN_OPEN);
    DrawScreen();
}

static void SelectModeButton(void)
{
    u8 newMode;
    u8 newDirectPage = sNamingScreen->directPage;

    switch (sNamingScreen->modeCursor)
    {
    case MODE_BUTTON_PINYIN:
        newMode = MODE_PINYIN;
        break;
    case MODE_BUTTON_SYMBOLS:
        newMode = MODE_DIRECT;
        newDirectPage = DIRECT_PAGE_SYMBOLS;
        break;
    case MODE_BUTTON_ABC:
    default:
        newMode = MODE_DIRECT;
        if (sNamingScreen->mode == MODE_DIRECT && sNamingScreen->directPage == DIRECT_PAGE_UPPER)
            newDirectPage = DIRECT_PAGE_LOWER;
        else
            newDirectPage = DIRECT_PAGE_UPPER;
        break;
    }

    SetInputMode(newMode, newDirectPage);
}

static void CycleInputMode(void)
{
    if (sNamingScreen->mode == MODE_PINYIN)
    {
        SetInputMode(MODE_DIRECT, DIRECT_PAGE_UPPER);
    }
    else if (sNamingScreen->directPage == DIRECT_PAGE_UPPER)
    {
        SetInputMode(MODE_DIRECT, DIRECT_PAGE_LOWER);
    }
    else if (sNamingScreen->directPage == DIRECT_PAGE_LOWER)
    {
        SetInputMode(MODE_DIRECT, DIRECT_PAGE_SYMBOLS);
    }
    else
    {
        SetInputMode(MODE_PINYIN, DIRECT_PAGE_UPPER);
    }
}

static u8 GetCandidatesOnCurrentPage(void)
{
    u8 start = sNamingScreen->candidatePage * CANDIDATES_PER_PAGE;

    if (sNamingScreen->mode == MODE_DIRECT || sNamingScreen->candidateCount <= start)
        return 0;
    if (sNamingScreen->candidateCount - start > CANDIDATES_PER_PAGE)
        return CANDIDATES_PER_PAGE;
    return sNamingScreen->candidateCount - start;
}

static u8 GetCandidatePageCount(void)
{
    if (sNamingScreen->mode == MODE_DIRECT || sNamingScreen->candidateCount == 0)
        return 0;
    return (sNamingScreen->candidateCount + CANDIDATES_PER_PAGE - 1) / CANDIDATES_PER_PAGE;
}

static bool8 HasMultipleCandidatePages(void)
{
    return GetCandidatePageCount() > 1;
}

static bool8 ChangeCandidatePage(s8 delta, bool8 keepCursor)
{
    u8 pageCount = GetCandidatePageCount();

    if (pageCount <= 1)
        return FALSE;

    if (delta < 0)
    {
        if (sNamingScreen->candidatePage == 0)
            return FALSE;
        sNamingScreen->candidatePage--;
        if (!keepCursor)
            sNamingScreen->cursorX = GetCandidatesOnCurrentPage() - 1;
    }
    else
    {
        if (sNamingScreen->candidatePage >= pageCount - 1)
            return FALSE;
        sNamingScreen->candidatePage++;
        if (!keepCursor)
            sNamingScreen->cursorX = 0;
    }

    ClampCandidateCursor();
    PlaySE(SE_SELECT);
    DrawTitle();
    DrawPinyinBuffer();
    DrawCandidates();
    UpdateCursorSprites();
    CopyBgTilemapBufferToVram(0);
    return TRUE;
}

static void ClampCandidateCursor(void)
{
    u8 count = GetCandidatesOnCurrentPage();

    if (count == 0)
    {
        if (sNamingScreen->cursorRegion == REGION_CANDIDATES)
        {
            sNamingScreen->cursorRegion = REGION_KEYS;
            sNamingScreen->cursorX = 0;
            sNamingScreen->cursorY = 0;
        }
        return;
    }

    if (sNamingScreen->cursorRegion == REGION_CANDIDATES && sNamingScreen->cursorX >= count)
        sNamingScreen->cursorX = count - 1;
}

static void CreateNamingScreenTask(void)
{
    CreateTask(Task_NamingScreen, 2);
    SetVBlankCallback(VBlankCB_NamingScreen);
    SetMainCallback2(CB2_NamingScreen);
}

static void Task_NamingScreen(u8 taskId)
{
    switch (sNamingScreen->state)
    {
    case STATE_FADE_IN:
        BlendPalettes(PALETTES_ALL, 16, RGB_BLACK);
        BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
        sNamingScreen->state++;
        break;
    case STATE_WAIT_FADE_IN:
        if (!gPaletteFade.active)
            sNamingScreen->state++;
        break;
    case STATE_HANDLE_INPUT:
        HandleInput();
        UpdateDeleteCursorFlash();
        if (++sNamingScreen->textEntryAnimDelay >= TEXT_ENTRY_ANIM_DELAY)
        {
            sNamingScreen->textEntryAnimDelay = 0;
            sNamingScreen->textEntryAnimFrame = (sNamingScreen->textEntryAnimFrame + 1) & 3;
            DrawTextEntry();
            CopyBgTilemapBufferToVram(0);
        }
        break;
    case STATE_PRESSED_OK:
        SaveInputText();
        sNamingScreen->state++;
        break;
    case STATE_FADE_OUT:
        BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
        sNamingScreen->state++;
        break;
    case STATE_EXIT:
        if (!gPaletteFade.active)
        {
            if (sNamingScreen->templateNum == NAMING_SCREEN_PLAYER)
                SeedRngAndSetTrainerId();
            if (sNamingScreen->templateNum == NAMING_SCREEN_CAUGHT_MON
             && CalculatePlayerPartyCount() < PARTY_SIZE)
                SetMainCallback2(BattleMainCB2);
            else
                SetMainCallback2(sNamingScreen->returnCallback);
            DestroyTask(taskId);
            FreeAllWindowBuffers();
            FREE_AND_SET_NULL(sNamingScreen);
        }
        break;
    }
}

static void HandleInput(void)
{
    if (JOY_REPEAT(DPAD_LEFT))
    {
        MoveCursor(-1, 0);
    }
    else if (JOY_REPEAT(DPAD_RIGHT))
    {
        MoveCursor(1, 0);
    }
    else if (JOY_REPEAT(DPAD_UP))
    {
        MoveCursor(0, -1);
    }
    else if (JOY_REPEAT(DPAD_DOWN))
    {
        MoveCursor(0, 1);
    }
    else if (JOY_NEW(A_BUTTON))
    {
        ActivateSelection();
    }
    else if (JOY_NEW(B_BUTTON))
    {
        if (DeleteInput())
            StartDeleteCursorFlash();
    }
    else if (JOY_NEW(L_BUTTON))
    {
        if (sNamingScreen->mode != MODE_DIRECT)
            ChangeCandidatePage(-1, TRUE);
    }
    else if (JOY_NEW(R_BUTTON))
    {
        if (sNamingScreen->mode != MODE_DIRECT)
            ChangeCandidatePage(1, TRUE);
    }
    else if (JOY_NEW(SELECT_BUTTON))
    {
        CycleInputMode();
    }
    else if (JOY_NEW(START_BUTTON))
    {
        PlaySE(SE_SELECT);
        sNamingScreen->cursorRegion = REGION_OK;
        UpdateCursorSprites();
    }
}

static void MoveCursor(s8 dx, s8 dy)
{
    u8 oldRegion = sNamingScreen->cursorRegion;
    u8 oldCursorX = sNamingScreen->cursorX;
    u8 oldCursorY = sNamingScreen->cursorY;
    u8 oldModeCursor = sNamingScreen->modeCursor;
    u8 oldCandidatePage = sNamingScreen->candidatePage;
    u8 candidateCount;

    if (dx != 0)
    {
        switch (sNamingScreen->cursorRegion)
        {
        case REGION_KEYS:
            sNamingScreen->cursorX = (sNamingScreen->cursorX + GetKeyboardRowKeyCount(sNamingScreen->cursorY) + dx) % GetKeyboardRowKeyCount(sNamingScreen->cursorY);
            break;
        case REGION_CANDIDATES:
            candidateCount = GetCandidatesOnCurrentPage();
            if (candidateCount != 0)
            {
                if (dx < 0)
                {
                    if (sNamingScreen->cursorX != 0)
                        sNamingScreen->cursorX--;
                    else if (ChangeCandidatePage(-1, FALSE))
                        return;
                }
                else
                {
                    if (sNamingScreen->cursorX + 1 < candidateCount)
                        sNamingScreen->cursorX++;
                    else if (ChangeCandidatePage(1, FALSE))
                        return;
                }
            }
            break;
        case REGION_BACK:
            sNamingScreen->cursorRegion = (dx > 0) ? REGION_OK : REGION_MODE;
            if (sNamingScreen->cursorRegion == REGION_MODE)
                sNamingScreen->modeCursor = MODE_BUTTON_COUNT - 1;
            break;
        case REGION_MODE:
            if (dx > 0)
            {
                if (sNamingScreen->modeCursor < MODE_BUTTON_COUNT - 1)
                    sNamingScreen->modeCursor++;
                else
                    sNamingScreen->cursorRegion = REGION_BACK;
            }
            else
            {
                if (sNamingScreen->modeCursor != 0)
                    sNamingScreen->modeCursor--;
                else
                    sNamingScreen->cursorRegion = REGION_OK;
            }
            break;
        case REGION_OK:
            sNamingScreen->cursorRegion = (dx > 0) ? REGION_MODE : REGION_BACK;
            if (sNamingScreen->cursorRegion == REGION_MODE)
                sNamingScreen->modeCursor = MODE_BUTTON_PINYIN;
            break;
        }
    }
    else if (dy != 0)
    {
        switch (sNamingScreen->cursorRegion)
        {
        case REGION_KEYS:
            if (dy < 0 && sNamingScreen->cursorY == 0)
            {
                if (GetCandidatesOnCurrentPage() != 0)
                {
                    sNamingScreen->cursorRegion = REGION_CANDIDATES;
                    sNamingScreen->cursorX = min(sNamingScreen->cursorX, GetCandidatesOnCurrentPage() - 1);
                    sNamingScreen->cursorY = 0;
                }
            }
            else if (dy > 0 && sNamingScreen->cursorY == PINYIN_KEY_ROWS - 1)
            {
                sNamingScreen->cursorRegion = REGION_MODE;
                sNamingScreen->modeCursor = MODE_BUTTON_PINYIN;
                sNamingScreen->cursorY = 0;
            }
            else
            {
                sNamingScreen->cursorY = (sNamingScreen->cursorY + PINYIN_KEY_ROWS + dy) % PINYIN_KEY_ROWS;
                ClampKeyboardCursor();
            }
            break;
        case REGION_CANDIDATES:
            if (dy > 0 && sNamingScreen->cursorY == CANDIDATE_ROWS - 1)
            {
                sNamingScreen->cursorRegion = REGION_KEYS;
                sNamingScreen->cursorY = 0;
                ClampKeyboardCursor();
            }
            else
            {
                sNamingScreen->cursorY = (sNamingScreen->cursorY + CANDIDATE_ROWS + dy) % CANDIDATE_ROWS;
            }
            break;
        default:
            if (dy < 0)
            {
                sNamingScreen->cursorRegion = REGION_KEYS;
                sNamingScreen->cursorY = PINYIN_KEY_ROWS - 1;
                ClampKeyboardCursor();
            }
            else
            {
                if (GetCandidatesOnCurrentPage() != 0)
                    sNamingScreen->cursorRegion = REGION_CANDIDATES;
                else
                {
                    sNamingScreen->cursorRegion = REGION_KEYS;
                }
                sNamingScreen->cursorY = 0;
                ClampKeyboardCursor();
            }
            break;
        }
    }

    if (oldRegion != sNamingScreen->cursorRegion
     || oldCursorX != sNamingScreen->cursorX
     || oldCursorY != sNamingScreen->cursorY
     || oldModeCursor != sNamingScreen->modeCursor
     || oldCandidatePage != sNamingScreen->candidatePage)
    {
        PlaySE(SE_SELECT);
        DrawKeyboard();
        DrawCandidates();
        UpdateCursorSprites();
        CopyBgTilemapBufferToVram(0);
    }
}

static void ActivateSelection(void)
{
    u8 index;

    switch (sNamingScreen->cursorRegion)
    {
    case REGION_KEYS:
        index = GetKeyboardIndexAtCursor();
        if (sNamingScreen->mode == MODE_DIRECT)
        {
            if (IsKeyboardIndexValid(index))
                AddDirectCharacter(sDirectKeyChars[sNamingScreen->directPage][index]);
        }
        else if (IsKeyboardIndexValid(index) && sNamingScreen->pinyinLength < PINYIN_BUFFER_LENGTH)
        {
            sNamingScreen->pinyinAscii[sNamingScreen->pinyinLength] = sPinyinKeysAscii[index];
            sNamingScreen->pinyinDisplay[sNamingScreen->pinyinLength] = sPinyinKeysDisplay[index];
            sNamingScreen->pinyinLength++;
            sNamingScreen->pinyinAscii[sNamingScreen->pinyinLength] = 0;
            sNamingScreen->pinyinDisplay[sNamingScreen->pinyinLength] = EOS;
            ResetCandidateSelection();
            PlaySE(SE_SELECT);
            DrawTitle();
            DrawPinyinBuffer();
            DrawCandidates();
            UpdateCursorSprites();
            CopyBgTilemapBufferToVram(0);
        }
        break;
    case REGION_CANDIDATES:
        if (sNamingScreen->mode == MODE_DIRECT)
            return;
        index = GetCandidateIndexAtCursor();
        if (index < sNamingScreen->candidateCount)
        {
            if (AddCandidateCharacter(GetEncodedCharAt(sNamingScreen->candidates, index)))
            {
                ClearPinyinBuffer();
                ResetCandidateSelection();
                DrawTitle();
                DrawPinyinBuffer();
                DrawCandidates();
                UpdateCursorSprites();
                CopyBgTilemapBufferToVram(0);
            }
        }
        break;
    case REGION_BACK:
        DeleteInput();
        break;
    case REGION_MODE:
        SelectModeButton();
        break;
    case REGION_OK:
        PlaySE(SE_SELECT);
        sNamingScreen->state = STATE_PRESSED_OK;
        break;
    }
}

static bool8 DeleteInput(void)
{
    u8 len;

    if (DeletePinyinCharacter())
        return TRUE;

    if (sNamingScreen->textByteLength == 0 || sNamingScreen->textCharCount == 0)
        return FALSE;

    len = sNamingScreen->textByteLength - 1;
    while (len > 0 && sNamingScreen->textBuffer[len - 1] >= 0x01 && sNamingScreen->textBuffer[len - 1] <= 0x1E
        && sNamingScreen->textBuffer[len - 1] != 0x06 && sNamingScreen->textBuffer[len - 1] != 0x1B)
        len--;

    sNamingScreen->textByteLength = len;
    sNamingScreen->textBuffer[len] = EOS;
    sNamingScreen->textCharCount--;
    PlaySE(SE_BALL);
    DrawTextEntry();
    CopyBgTilemapBufferToVram(0);
    return TRUE;
}

static bool8 DeletePinyinCharacter(void)
{
    if (sNamingScreen->pinyinLength == 0)
        return FALSE;

    sNamingScreen->pinyinLength--;
    sNamingScreen->pinyinAscii[sNamingScreen->pinyinLength] = 0;
    sNamingScreen->pinyinDisplay[sNamingScreen->pinyinLength] = EOS;
    ResetCandidateSelection();
    PlaySE(SE_BALL);
    DrawTitle();
    DrawPinyinBuffer();
    DrawCandidates();
    UpdateCursorSprites();
    CopyBgTilemapBufferToVram(0);
    return TRUE;
}

static void StartDeleteCursorFlash(void)
{
    sNamingScreen->deleteCursorFlashTimer = PINYIN_DELETE_CURSOR_FLASH_FRAMES;
    UpdateCursorSprites();
}

static void UpdateDeleteCursorFlash(void)
{
    if (sNamingScreen->deleteCursorFlashTimer == 0)
        return;

    sNamingScreen->deleteCursorFlashTimer--;
    if (sNamingScreen->deleteCursorFlashTimer == 0)
        UpdateCursorSprites();
}

static bool8 AddDirectCharacter(u8 ch)
{
    u8 text[2];
    bool8 added;

    text[0] = ch;
    text[1] = EOS;
    added = AppendTextChar(text);

    if (added)
    {
        PlaySE(SE_SELECT);
        DrawTextEntry();
        CopyBgTilemapBufferToVram(0);
    }
    return added;
}

static bool8 AddCandidateCharacter(const u8 *ch)
{
    bool8 added = AppendTextChar(ch);

    if (added)
    {
        PlaySE(SE_SELECT);
        DrawTextEntry();
    }
    return added;
}

static bool8 AppendTextChar(const u8 *ch)
{
    u8 needed = GetEncodedCharByteLength(ch);

    if (sNamingScreen->textCharCount >= sNamingScreen->template->maxChars)
        return FALSE;
    if (sNamingScreen->textByteLength + needed > sNamingScreen->template->maxChars)
        return FALSE;
    if (sNamingScreen->textByteLength + needed >= sizeof(sNamingScreen->textBuffer))
        return FALSE;

    sNamingScreen->textBuffer[sNamingScreen->textByteLength] = ch[0];
    if (needed == 2)
        sNamingScreen->textBuffer[sNamingScreen->textByteLength + 1] = ch[1];
    sNamingScreen->textByteLength += needed;
    sNamingScreen->textBuffer[sNamingScreen->textByteLength] = EOS;
    sNamingScreen->textCharCount++;
    return TRUE;
}

static void SaveInputText(void)
{
    u8 i;

    for (i = 0; i < sNamingScreen->textByteLength; i++)
    {
        if (sNamingScreen->textBuffer[i] != CHAR_SPACE && sNamingScreen->textBuffer[i] != EOS)
        {
            StringCopyN(sNamingScreen->destBuffer, sNamingScreen->textBuffer, sNamingScreen->template->maxChars + 1);
            sNamingScreen->destBuffer[sNamingScreen->template->maxChars] = EOS;
            return;
        }
    }
}

static void DrawScreen(void)
{
    DrawTitle();
    DrawTextEntry();
    DrawPinyinBuffer();
    DrawKeyboard();
    DrawCandidates();
    DrawControls();
    UpdateCursorSprites();
    CopyBgTilemapBufferToVram(0);
}

static void DrawTitle(void)
{
    u8 buffer[64];
    u8 x;
    const u8 *title = sNamingScreen->template->title;

    FillWindowPixelBuffer(sNamingScreen->windows[WIN_TITLE], PIXEL_FILL(0));
    if (sNamingScreen->templateNum == NAMING_SCREEN_CAUGHT_MON || sNamingScreen->templateNum == NAMING_SCREEN_NICKNAME)
    {
        u8 *end = StringCopy(buffer, GetSpeciesName(sNamingScreen->monSpecies));
        end = StringAppend(end, title);
        WrapFontIdToFit(buffer, end, FONT_SMALL, 152);
        title = buffer;
    }
    AddTextPrinterParameterized4(sNamingScreen->windows[WIN_TITLE], FONT_SMALL, TITLE_TEXT_LEFT, TITLE_TEXT_TOP, 0, 0, sTextColors, TEXT_SKIP_DRAW, title);

    if (HasMultipleCandidatePages())
    {
        x = GetStringRightAlignXOffset(FONT_SMALL, sText_DirectHint, PINYIN_TOP_TEXT_WIDTH);
        AddTextPrinterParameterized4(sNamingScreen->windows[WIN_TITLE], FONT_SMALL, x, TITLE_TEXT_TOP, 0, 0, sTextColors, TEXT_SKIP_DRAW, sText_DirectHint);
    }

    PutWindowTilemap(sNamingScreen->windows[WIN_TITLE]);
    CopyWindowToVram(sNamingScreen->windows[WIN_TITLE], COPYWIN_FULL);
}

static void DrawTextEntry(void)
{
    u8 i;
    u8 text[3];
    u8 slot = 0;
    u8 textOffset = 0;
    u8 maxChars = sNamingScreen->template->maxChars;
    u8 baseX = (DISPLAY_WIDTH - maxChars * TEXT_ENTRY_SLOT_WIDTH) / 2 + 6 - TEXT_ENTRY_WINDOW_LEFT;

    FillWindowPixelBuffer(sNamingScreen->windows[WIN_TEXT_ENTRY], PIXEL_FILL(0));

    for (i = 0; i < maxChars; i++)
    {
        FillWindowPixelRect(
            sNamingScreen->windows[WIN_TEXT_ENTRY],
            PIXEL_FILL(1),
            baseX + i * TEXT_ENTRY_SLOT_WIDTH + TEXT_ENTRY_UNDERLINE_X_OFFSET,
            TEXT_ENTRY_UNDERLINE_Y,
            TEXT_ENTRY_UNDERLINE_WIDTH,
            1);
    }

    while (slot < maxChars && textOffset < sNamingScreen->textByteLength)
    {
        u8 byteLength = GetEncodedCharByteLength(&sNamingScreen->textBuffer[textOffset]);
        u8 charWidth;
        u8 charX;

        text[0] = sNamingScreen->textBuffer[textOffset];
        if (byteLength == 2)
        {
            text[1] = sNamingScreen->textBuffer[textOffset + 1];
            text[2] = EOS;
        }
        else
        {
            text[1] = EOS;
        }

        charWidth = GetStringWidth(FONT_NORMAL, text, 0);
        charX = baseX + slot * TEXT_ENTRY_SLOT_WIDTH;
        if (byteLength == 2 && charWidth < TEXT_ENTRY_SLOT_WIDTH * 2)
            charX += (TEXT_ENTRY_SLOT_WIDTH * 2 - charWidth) / 2;

        AddTextPrinterParameterized4(
            sNamingScreen->windows[WIN_TEXT_ENTRY],
            FONT_NORMAL,
            charX,
            TEXT_ENTRY_TEXT_Y,
            0,
            0,
            sTextColors,
            TEXT_SKIP_DRAW,
            text);
        textOffset += byteLength;
        slot += byteLength;
    }

    if (slot < maxChars)
    {
        u8 caretX = baseX + slot * TEXT_ENTRY_SLOT_WIDTH;

        if ((sNamingScreen->textEntryAnimFrame & 2) == 0)
        {
            FillWindowPixelRect(
                sNamingScreen->windows[WIN_TEXT_ENTRY],
                PIXEL_FILL(1),
                caretX + TEXT_ENTRY_UNDERLINE_X_OFFSET,
                TEXT_ENTRY_ACTIVE_LINE_Y,
                TEXT_ENTRY_UNDERLINE_WIDTH,
                2);
        }
    }

    if (sNamingScreen->template->addGenderIcon && sNamingScreen->monGender != MON_GENDERLESS)
    {
        bool8 isFemale = (sNamingScreen->monGender == MON_FEMALE);
        const u8 *gender = isFemale ? gText_FemaleSymbol : gText_MaleSymbol;
        AddTextPrinterParameterized4(sNamingScreen->windows[WIN_TEXT_ENTRY], FONT_NORMAL, 124, 1, 0, 0, sGenderColors[isFemale], TEXT_SKIP_DRAW, gender);
    }

    PutWindowTilemap(sNamingScreen->windows[WIN_TEXT_ENTRY]);
    CopyWindowToVram(sNamingScreen->windows[WIN_TEXT_ENTRY], COPYWIN_FULL);
}

static void DrawPinyinBuffer(void)
{
    FillWindowPixelBuffer(sNamingScreen->windows[WIN_PINYIN], PIXEL_FILL(0));

    if (sNamingScreen->mode != MODE_PINYIN || sNamingScreen->pinyinLength == 0)
    {
        ClearWindowTilemap(sNamingScreen->windows[WIN_PINYIN]);
        CopyWindowToVram(sNamingScreen->windows[WIN_PINYIN], COPYWIN_MAP);
        return;
    }

    AddTextPrinterParameterized4(sNamingScreen->windows[WIN_PINYIN], FONT_NORMAL, PINYIN_BUFFER_TEXT_X, PINYIN_BUFFER_TEXT_Y, 0, 0, sTextColors, TEXT_SKIP_DRAW, sNamingScreen->pinyinDisplay);
    PutWindowTilemap(sNamingScreen->windows[WIN_PINYIN]);
    CopyWindowToVram(sNamingScreen->windows[WIN_PINYIN], COPYWIN_FULL);
}

static void DrawKeyboard(void)
{
    u8 i;
    u8 x;
    u8 y;
    u8 text[2];
    const u8 *keys;

    if (sNamingScreen->mode == MODE_PINYIN)
        keys = sPinyinKeysDisplay;
    else
        keys = sDirectKeyChars[sNamingScreen->directPage];

    FillWindowPixelBuffer(sNamingScreen->windows[WIN_KEYBOARD], PIXEL_FILL(0));
    for (i = 0; i < PINYIN_KEY_COUNT; i++)
    {
        if (!IsKeyboardIndexValid(i))
            continue;

        x = (i % PINYIN_KEYS_PER_ROW) * KEYBOARD_KEY_STEP_X;
        y = (i / PINYIN_KEYS_PER_ROW) * KEYBOARD_KEY_STEP_Y;
        text[0] = keys[i];
        text[1] = EOS;
        AddTextPrinterParameterized4(
            sNamingScreen->windows[WIN_KEYBOARD],
            FONT_NORMAL,
            x + 7,
            y + 3,
            0,
            0,
            sTextColors,
            TEXT_SKIP_DRAW,
            text);
    }

    PutWindowTilemap(sNamingScreen->windows[WIN_KEYBOARD]);
    CopyWindowToVram(sNamingScreen->windows[WIN_KEYBOARD], COPYWIN_FULL);
}

static void DrawCandidates(void)
{
    u8 i;
    u8 index;
    u8 x;
    u8 y;
    u8 text[3];
    u8 count;

    FillWindowPixelBuffer(sNamingScreen->windows[WIN_CANDIDATES], PIXEL_FILL(0));
    ClampCandidateCursor();

    if (sNamingScreen->mode == MODE_DIRECT)
    {
    }
    else if (sNamingScreen->pinyinLength == 0)
    {
        AddTextPrinterParameterized4(sNamingScreen->windows[WIN_CANDIDATES], FONT_NORMAL, 0, 3, 0, 0, sMutedTextColors, TEXT_SKIP_DRAW, gText_ExpandedPlaceholder_Empty);
    }
    else
    {
        count = GetCandidatesOnCurrentPage();
        for (i = 0; i < count; i++)
        {
            const u8 *candidate;
            u8 byteLength;

            index = sNamingScreen->candidatePage * CANDIDATES_PER_PAGE + i;
            x = (i % CANDIDATE_COLS) * KEYBOARD_KEY_STEP_X;
            y = (i / CANDIDATE_COLS) * KEYBOARD_KEY_STEP_Y;
            candidate = GetEncodedCharAt(sNamingScreen->candidates, index);
            byteLength = GetEncodedCharByteLength(candidate);
            text[0] = candidate[0];
            if (byteLength == 2)
                text[1] = candidate[1];
            text[byteLength] = EOS;
            AddTextPrinterParameterized4(sNamingScreen->windows[WIN_CANDIDATES], FONT_NORMAL, x + 4, y + 3, 0, 0, sTextColors, TEXT_SKIP_DRAW, text);
        }
    }

    PutWindowTilemap(sNamingScreen->windows[WIN_CANDIDATES]);
    CopyWindowToVram(sNamingScreen->windows[WIN_CANDIDATES], COPYWIN_FULL);
    DrawControls();
    UpdateCursorSprites();
}

static void DrawControls(void)
{
    FillWindowPixelBuffer(sNamingScreen->windows[WIN_CONTROLS], PIXEL_FILL(0));

    DrawControlText(GetModeButtonLabel(MODE_BUTTON_PINYIN), PINYIN_MODE_BUTTON_PY_LEFT, PINYIN_MODE_BUTTON_WIDTH);
    DrawControlText(GetModeButtonLabel(MODE_BUTTON_ABC), PINYIN_MODE_BUTTON_ABC_LEFT, PINYIN_MODE_BUTTON_WIDTH);
    DrawControlText(GetModeButtonLabel(MODE_BUTTON_SYMBOLS), PINYIN_MODE_BUTTON_SYMBOL_LEFT, PINYIN_MODE_BUTTON_WIDTH);
    DrawControlText(sText_Backspace, PINYIN_DELETE_BUTTON_LEFT, PINYIN_ACTION_BUTTON_WIDTH);
    DrawControlText(sText_Ok, PINYIN_OK_BUTTON_LEFT, PINYIN_ACTION_BUTTON_WIDTH);

    PutWindowTilemap(sNamingScreen->windows[WIN_CONTROLS]);
    CopyWindowToVram(sNamingScreen->windows[WIN_CONTROLS], COPYWIN_FULL);
    UpdateCursorSprites();
}

static void DrawControlText(const u8 *text, u8 left, u8 width)
{
    u8 x = left - PINYIN_CONTROLS_LEFT + GetStringCenterAlignXOffset(FONT_SMALL, text, width);

    AddTextPrinterParameterized4(sNamingScreen->windows[WIN_CONTROLS], FONT_SMALL, x, PINYIN_CONTROL_TEXT_Y, 0, 0,
        sTextColors,
        TEXT_SKIP_DRAW, text);
}

static void ResetCandidateSelection(void)
{
    sNamingScreen->candidatePage = 0;
    UpdateCandidates();
    ClampCandidateCursor();
    if (sNamingScreen->cursorRegion == REGION_CANDIDATES)
    {
        sNamingScreen->cursorX = 0;
        sNamingScreen->cursorY = 0;
    }
}

static void UpdateCandidates(void)
{
    const struct PinyinCandidateGroup *group = FindCandidateGroup(sNamingScreen->pinyinAscii);

    if (group == NULL)
    {
        sNamingScreen->candidates = NULL;
        sNamingScreen->candidateCount = 0;
    }
    else
    {
        sNamingScreen->candidates = group->chars;
        sNamingScreen->candidateCount = CountEncodedChars(group->chars);
    }
}

static const struct PinyinCandidateGroup *FindCandidateGroup(const u8 *pinyin)
{
    u16 i;

    if (pinyin[0] == 0)
        return NULL;

    for (i = 0; i < ARRAY_COUNT(sPinyinCandidateGroups); i++)
    {
        if (PinyinAsciiEquals(sPinyinCandidateGroups[i].pinyin, pinyin))
            return &sPinyinCandidateGroups[i];
    }
    return NULL;
}

static bool8 PinyinAsciiEquals(const u8 *left, const u8 *right)
{
    while (*left == *right)
    {
        if (*left == 0)
            return TRUE;
        left++;
        right++;
    }
    return FALSE;
}

static void ClearPinyinBuffer(void)
{
    sNamingScreen->pinyinLength = 0;
    sNamingScreen->pinyinAscii[0] = 0;
    sNamingScreen->pinyinDisplay[0] = EOS;
}

static bool8 IsKeyboardIndexValid(u8 index)
{
    const u8 *keys;

    if (index >= PINYIN_KEY_COUNT)
        return FALSE;

    if (sNamingScreen->mode == MODE_PINYIN)
        keys = sPinyinKeysDisplay;
    else
        keys = sDirectKeyChars[sNamingScreen->directPage];

    return keys[index] != EOS;
}

static u8 GetKeyboardRowKeyCount(u8 row)
{
    if (sNamingScreen->mode == MODE_DIRECT && sNamingScreen->directPage == DIRECT_PAGE_SYMBOLS)
        return PINYIN_KEYS_PER_ROW;

    if (row == PINYIN_KEY_ROWS - 1)
        return 6;

    return PINYIN_KEYS_PER_ROW;
}

static void ClampKeyboardCursor(void)
{
    u8 rowKeyCount;

    if (sNamingScreen->cursorRegion != REGION_KEYS)
        return;

    if (sNamingScreen->cursorY >= PINYIN_KEY_ROWS)
        sNamingScreen->cursorY = PINYIN_KEY_ROWS - 1;

    rowKeyCount = GetKeyboardRowKeyCount(sNamingScreen->cursorY);
    if (sNamingScreen->cursorX >= rowKeyCount)
        sNamingScreen->cursorX = rowKeyCount - 1;
}

static u8 GetKeyboardIndexAtCursor(void)
{
    u8 index = sNamingScreen->cursorY * PINYIN_KEYS_PER_ROW + sNamingScreen->cursorX;

    if (index >= PINYIN_KEY_COUNT)
        index = 0;
    return index;
}

static u8 GetCandidateIndexAtCursor(void)
{
    return sNamingScreen->candidatePage * CANDIDATES_PER_PAGE + sNamingScreen->cursorY * CANDIDATE_COLS + sNamingScreen->cursorX;
}

static u8 GetEncodedCharByteLength(const u8 *str)
{
    if (str[0] >= 0x01 && str[0] <= 0x1E && str[0] != 0x06 && str[0] != 0x1B && str[1] <= 0xF6)
        return 2;
    return 1;
}

static u8 CountEncodedChars(const u8 *str)
{
    u8 count = 0;
    u16 byteLength = 0;

    while (str[byteLength] != EOS)
    {
        byteLength += GetEncodedCharByteLength(&str[byteLength]);
        count++;
    }
    return count;
}

static const u8 *GetEncodedCharAt(const u8 *str, u8 index)
{
    while (index != 0 && *str != EOS)
    {
        str += GetEncodedCharByteLength(str);
        index--;
    }
    return str;
}

static u8 CountTextChars(const u8 *str, u8 byteLimit)
{
    u8 count = 0;
    u8 byteLength = 0;

    while (byteLength < byteLimit && str[byteLength] != EOS)
    {
        byteLength += GetEncodedCharByteLength(&str[byteLength]);
        count++;
    }
    return count;
}

static void CopyExistingString(void)
{
    u8 i;

    for (i = 0; i < sNamingScreen->template->maxChars && sNamingScreen->destBuffer[i] != EOS; i++)
        sNamingScreen->textBuffer[i] = sNamingScreen->destBuffer[i];
    sNamingScreen->textBuffer[i] = EOS;
    sNamingScreen->textByteLength = i;
    sNamingScreen->textCharCount = CountTextChars(sNamingScreen->textBuffer, sNamingScreen->textByteLength);
}

static void NamingScreen_NoIcon(void);
static void NamingScreen_CreatePlayerIcon(void);
static void NamingScreen_CreatePCIcon(void);
static void NamingScreen_CreateMonIcon(void);
static void NamingScreen_CreateWaldaDadIcon(void);
static void NamingScreen_CreateCodeIcon(void);
static void NamingScreen_CreateRivalIcon(void);

static void (*const sIconFunctions[])(void) =
{
    NamingScreen_NoIcon,
    NamingScreen_CreatePlayerIcon,
    NamingScreen_CreatePCIcon,
    NamingScreen_CreateMonIcon,
    NamingScreen_CreateWaldaDadIcon,
    NamingScreen_CreateCodeIcon,
    NamingScreen_CreateRivalIcon,
};

static void CreateInputTargetIcon(void)
{
    sIconFunctions[sNamingScreen->template->iconFunction]();
}

static void NamingScreen_NoIcon(void)
{

}

static void NamingScreen_CreatePlayerIcon(void)
{
    u16 rivalGfxId;
    u8 spriteId;

    rivalGfxId = GetRivalAvatarGraphicsIdByStateIdAndGender(PLAYER_AVATAR_STATE_NORMAL, (enum Gender)sNamingScreen->monSpecies);
    spriteId = CreateObjectGraphicsSprite(rivalGfxId, SpriteCallbackDummy, INPUT_ICON_X, INPUT_ICON_Y, 0);
    gSprites[spriteId].oam.priority = 3;
    StartSpriteAnim(&gSprites[spriteId], ANIM_STD_GO_SOUTH);
}

static const struct Subsprite sSubsprites_PCIcon[] =
{
    {.x = -8, .y = -12, .shape = SPRITE_SHAPE(16x8), .size = SPRITE_SIZE(16x8), .tileOffset = 0, .priority = 3},
    {.x = -8, .y =  -4, .shape = SPRITE_SHAPE(16x8), .size = SPRITE_SIZE(16x8), .tileOffset = 2, .priority = 3},
    {.x = -8, .y =   4, .shape = SPRITE_SHAPE(16x8), .size = SPRITE_SIZE(16x8), .tileOffset = 4, .priority = 3},
};

static const struct SubspriteTable sSubspriteTable_PCIcon[] =
{
    {ARRAY_COUNT(sSubsprites_PCIcon), sSubsprites_PCIcon}
};

static const struct SpriteFrameImage sImageTable_PCIcon[] =
{
    {sPCIconOff_Gfx, sizeof(sPCIconOff_Gfx)},
    {sPCIconOn_Gfx, sizeof(sPCIconOn_Gfx)},
};

static const union AnimCmd sAnim_PCIcon[] =
{
    ANIMCMD_FRAME(0, 2),
    ANIMCMD_FRAME(1, 2),
    ANIMCMD_JUMP(0)
};

static const union AnimCmd *const sAnims_PCIcon[] =
{
    sAnim_PCIcon
};

static const struct OamData sOam_8x8 =
{
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_NORMAL,
    .bpp = ST_OAM_4BPP,
    .shape = SPRITE_SHAPE(8x8),
    .size = SPRITE_SIZE(8x8),
};

static const struct SpriteTemplate sSpriteTemplate_PCIcon =
{
    .tileTag = TAG_NONE,
    .paletteTag = PALTAG_PC_ICON,
    .oam = &sOam_8x8,
    .anims = sAnims_PCIcon,
    .images = sImageTable_PCIcon,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = SpriteCallbackDummy
};

static void NamingScreen_CreatePCIcon(void)
{
    u8 spriteId;

    spriteId = CreateSprite(&sSpriteTemplate_PCIcon, INPUT_ICON_X, INPUT_PC_ICON_Y, 0);
    SetSubspriteTables(&gSprites[spriteId], sSubspriteTable_PCIcon);
    gSprites[spriteId].oam.priority = 3;
}

static void NamingScreen_CreateMonIcon(void)
{
    u8 spriteId;

    LoadMonIconPalettes();
    spriteId = CreateMonIcon(sNamingScreen->monSpecies, SpriteCallbackDummy, INPUT_ICON_X, INPUT_MON_ICON_Y, 0, sNamingScreen->monPersonality);
    gSprites[spriteId].oam.priority = 3;
}

static void NamingScreen_CreateWaldaDadIcon(void)
{
    u8 spriteId;

    spriteId = CreateObjectGraphicsSprite(OBJ_EVENT_GFX_MAN_1, SpriteCallbackDummy, INPUT_ICON_X, INPUT_ICON_Y, 0);
    gSprites[spriteId].oam.priority = 3;
    StartSpriteAnim(&gSprites[spriteId], ANIM_STD_GO_SOUTH);
}

static void NamingScreen_CreateCodeIcon(void)
{
    u8 spriteId;

    spriteId = CreateObjectGraphicsSprite(OBJ_EVENT_GFX_MYSTERY_GIFT_MAN, SpriteCallbackDummy, INPUT_ICON_X, INPUT_ICON_Y, 0);
    gSprites[spriteId].oam.priority = 3;
}

static const union AnimCmd sAnim_Rival[] =
{
    ANIMCMD_FRAME( 0, 10),
    ANIMCMD_FRAME(24, 10),
    ANIMCMD_FRAME( 0, 10),
    ANIMCMD_FRAME(32, 10),
    ANIMCMD_JUMP(0)
};

static const union AnimCmd *const sAnims_Rival[] =
{
    sAnim_Rival
};

static void NamingScreen_CreateRivalIcon(void)
{
    const struct SpriteSheet sheet = {
        sRival_Gfx, 0x900, 255
    };
    const struct SpritePalette palette = {
        sRival_Pal, 255
    };
    struct SpriteTemplate template;
    const struct SubspriteTable *subspriteTables;
    u8 spriteId;

    CopyObjectGraphicsInfoToSpriteTemplate(OBJ_EVENT_GFX_RED_NORMAL, SpriteCallbackDummy, &template, &subspriteTables);
    template.tileTag = sheet.tag;
    template.paletteTag = palette.tag;
    template.anims = sAnims_Rival;
    LoadSpriteSheet(&sheet);
    LoadSpritePalette(&palette);
    spriteId = CreateSprite(&template, INPUT_ICON_X, INPUT_ICON_Y, 0);
    gSprites[spriteId].oam.priority = 3;
}

static UNUSED void DisplaySentToPCMessage(void)
{
    u8 stringToDisplay = 0;

    if (!IsDestinationBoxFull())
    {
        StringCopy(gStringVar1, GetBoxNamePtr(VarGet(VAR_PC_BOX_TO_SEND_MON)));
        StringCopy(gStringVar2, sNamingScreen->destBuffer);
    }
    else
    {
        StringCopy(gStringVar1, GetBoxNamePtr(VarGet(VAR_PC_BOX_TO_SEND_MON)));
        StringCopy(gStringVar2, sNamingScreen->destBuffer);
        StringCopy(gStringVar3, GetBoxNamePtr(GetPCBoxToSendMon()));
        stringToDisplay = 2;
    }

    if (FlagGet(FLAG_SYS_PC_LANETTE))
        stringToDisplay++;

    StringExpandPlaceholders(gStringVar4, sTransferredToPCMessages[stringToDisplay]);
    DrawDialogueFrame(0, FALSE);
    gTextFlags.canABSpeedUpPrint = TRUE;
    AddTextPrinterParameterized2(0, FONT_NORMAL, gStringVar4, GetPlayerTextSpeedDelay(), 0, TEXT_COLOR_DARK_GRAY, TEXT_COLOR_WHITE, TEXT_COLOR_LIGHT_GRAY);
    CopyWindowToVram(0, COPYWIN_FULL);
}

static void CB2_NamingScreen(void)
{
    RunTasks();
    AnimateSprites();
    BuildOamBuffer();
    UpdatePaletteFade();
}

static void VBlankCB_NamingScreen(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
}

static void NamingScreen_ShowBgs(void)
{
    ShowBg(0);
    ShowBg(1);
}

static void UNUSED Debug_NamingScreenPlayer(void)
{
    DoNamingScreen(NAMING_SCREEN_PLAYER, gSaveBlock2Ptr->playerName, gSaveBlock2Ptr->playerGender, 0, 0, CB2_ReturnToFieldWithOpenMenu);
}

static const struct NamingScreenTemplate sPlayerNamingScreenTemplate =
{
    .copyExistingString = FALSE,
    .maxChars = PLAYER_NAME_LENGTH,
    .iconFunction = 1,
    .addGenderIcon = FALSE,
    .title = sText_PlayerNamePrompt,
};

static const struct NamingScreenTemplate sPCBoxNamingTemplate =
{
    .copyExistingString = FALSE,
    .maxChars = BOX_NAME_LENGTH,
    .iconFunction = 2,
    .addGenderIcon = FALSE,
    .title = COMPOUND_STRING("盒子的名字?"),
};

static const struct NamingScreenTemplate sMonNamingScreenTemplate =
{
    .copyExistingString = FALSE,
    .maxChars = POKEMON_NAME_LENGTH,
    .iconFunction = 3,
    .addGenderIcon = TRUE,
    .title = sText_MonNamePrompt,
};

static const struct NamingScreenTemplate sWaldaWordsScreenTemplate =
{
    .copyExistingString = TRUE,
    .maxChars = WALDA_PHRASE_LENGTH,
    .iconFunction = 4,
    .addGenderIcon = FALSE,
    .title = COMPOUND_STRING("输入笑话:"),
};

static const struct NamingScreenTemplate sCodeScreenTemplate =
{
    .copyExistingString = FALSE,
    .maxChars = CODE_NAME_LENGTH,
    .iconFunction = 5,
    .addGenderIcon = FALSE,
    .title = COMPOUND_STRING("输入代码:"),
};

static const struct NamingScreenTemplate sRivalNamingScreenTemplate =
{
    .copyExistingString = FALSE,
    .maxChars = PLAYER_NAME_LENGTH,
    .iconFunction = 6,
    .addGenderIcon = FALSE,
    .title = sText_RivalsName,
};

static const struct NamingScreenTemplate *const sNamingScreenTemplates[] =
{
    [NAMING_SCREEN_PLAYER]     = &sPlayerNamingScreenTemplate,
    [NAMING_SCREEN_BOX]        = &sPCBoxNamingTemplate,
    [NAMING_SCREEN_CAUGHT_MON] = &sMonNamingScreenTemplate,
    [NAMING_SCREEN_NICKNAME]   = &sMonNamingScreenTemplate,
    [NAMING_SCREEN_WALDA]      = &sWaldaWordsScreenTemplate,
    [NAMING_SCREEN_CODE]       = &sCodeScreenTemplate,
    [NAMING_SCREEN_RIVAL]      = &sRivalNamingScreenTemplate,
};
