#ifndef _CHRPROP_H_
#define _CHRPROP_H_
#include <ultra64.h>
#include <bondconstants.h>
#include <bondtypes.h>

bool doorIsPadlockFree(DoorRecord*door);
s32 propDoorGetCdTypes(PropRecord*prop);
s32 sub_GAME_7F03DB70(s32*roomids1, s32*roomids2);
void chrpropDeregisterRoom(PropRecord*prop, s16 room);
void chrpropDetach(PropRecord*prop);
void removed_debug_roomblocks_feature(void);

#endif
