#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "librfu.h"
#include "link_rfu.h"

static int sFailures;

#define CHECK(expression)                                                     \
    do                                                                        \
    {                                                                         \
        if (!(expression))                                                    \
        {                                                                     \
            fprintf(stderr, "runtime boundary smoke failed: %s:%d: %s\n",   \
                    __FILE__, __LINE__, #expression);                         \
            sFailures++;                                                      \
        }                                                                     \
    } while (0)

static void TestRfuUnavailable(void)
{
    struct RfuGameData gameData;
    u8 username[RFU_USER_NAME_LENGTH];

    gRfu.parentChild = MODE_PARENT;
    gRfu.status = RFU_STATUS_OK;
    InitializeRfuLinkManager_LinkLeader(2);
    CHECK(gRfu.parentChild == MODE_NEUTRAL);
    CHECK(gRfu.status == RFU_STATUS_CONNECTION_ERROR);

    gRfu.parentChild = MODE_CHILD;
    InitializeRfuLinkManager_JoinGroup();
    CHECK(gRfu.parentChild == MODE_NEUTRAL);
    CHECK(gRfu.status == RFU_STATUS_CONNECTION_ERROR);

    gRfu.parentChild = MODE_PARENT;
    InitializeRfuLinkManager_EnterUnionRoom();
    CHECK(gRfu.parentChild == MODE_NEUTRAL);
    CHECK(gRfu.status == RFU_STATUS_CONNECTION_ERROR);

    memset(&gameData, 0xA5, sizeof(gameData));
    memset(username, 0xA5, sizeof(username));
    CHECK(!Rfu_GetCompatiblePlayerData(&gameData, username, 0));
    for (size_t i = 0; i < RFU_GAME_NAME_LENGTH; i++)
        CHECK(((u8 *)&gameData)[i] == 0);
    for (size_t i = 0; i < sizeof(username); i++)
        CHECK(username[i] == 0);

    memset(&gameData, 0xA5, sizeof(gameData));
    memset(username, 0xA5, sizeof(username));
    CHECK(!Rfu_GetWonderDistributorPlayerData(&gameData, username, 0));
    for (size_t i = 0; i < RFU_GAME_NAME_LENGTH; i++)
        CHECK(((u8 *)&gameData)[i] == 0);
    for (size_t i = 0; i < sizeof(username); i++)
        CHECK(username[i] == 0);
}

int main(void)
{
    TestRfuUnavailable();
    if (sFailures != 0)
        return 1;
    puts("runtime boundary smoke checks passed");
    return 0;
}
