#include <stdio.h>
#include "NuMicro.h"
#include "ff.h"
#include <string.h>
#include "lauxlib.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "luaconf.h"


unsigned int GET_FILE_SIZE(void *buf)
{
    FIL file1;
    unsigned int file_size = 0;
    uint8_t *TempChar;

    FRESULT res;
    TempChar = (uint8_t *)buf;
    res = f_open(&file1, (const char *)TempChar, FA_OPEN_EXISTING | FA_READ);

    if (res == FR_OK)
    {
        file_size  = f_size(&file1);
    }

    f_close(&file1);
    return file_size;
}

unsigned int GET_FILE_CHECKSUM(void *buf)
{
    FIL file1;
    uint8_t *TempChar;
    FRESULT res;
    unsigned int binfile_checksum, s1;
    unsigned int temp_ct;
    unsigned char Buff[512];
    binfile_checksum = 0;
    TempChar = (uint8_t *)buf;
    res = f_open(&file1, (const char *)TempChar, FA_OPEN_EXISTING | FA_READ);

    if (res == FR_OK)
    {
        for (;;)
        {
            res = f_read(&file1, Buff, sizeof(Buff), &s1);

            if (res || s1 == 0) break;

            for (temp_ct = 0; temp_ct < s1; temp_ct++)
            {
                binfile_checksum = binfile_checksum + Buff[temp_ct];
            }
        }


        f_close(&file1);
    }

    return binfile_checksum;
}

 volatile unsigned int AP_file_totallen;
  unsigned int AP_file_checksum;


uint32_t config[2];
volatile unsigned char APROM_NAME[128];
char LUA_SCRIPT_GLOBAL[4096];



int ISP_AP_FILE_SIZE(lua_State *L)
{
    unsigned char FILE_NAME[128];

    const char *ttt;
    ttt = lua_tostring(L, 1);
    memset(FILE_NAME, '\0', sizeof(FILE_NAME));
    memcpy((unsigned char *)FILE_NAME, ttt, lua_rawlen(L, 1));

    AP_file_totallen = GET_FILE_SIZE((void *)FILE_NAME);
    //printf("APROM file size =0x%x\n\r",AP_file_totallen);
    lua_pushnumber(L, AP_file_totallen);
    return 1;
}

int ISP_AP_FILE_CHECKSUM(lua_State *L)
{
    unsigned char FILE_NAME[128];

    const char *ttt;
    ttt = lua_tostring(L, 1);
    memset(FILE_NAME, '\0', sizeof(FILE_NAME));
    memcpy((unsigned char *)FILE_NAME, ttt, lua_rawlen(L, 1));

    AP_file_checksum = GET_FILE_CHECKSUM((void *)FILE_NAME);
    //printf("APROM file checksum =0x%x\n\r",AP_file_checksum);
    lua_pushnumber(L, AP_file_checksum);
    return 1;
}



static const struct luaL_Reg mylib[] =
{
    {"ISP_AP_FILE_SIZE", ISP_AP_FILE_SIZE},
    {"ISP_AP_FILE_CHECKSUM", ISP_AP_FILE_CHECKSUM},
    {NULL, NULL}

};

int luaopen_mylib(lua_State *L)
{
    luaL_setfuncs(L, mylib, 0);
    return 1;
}
