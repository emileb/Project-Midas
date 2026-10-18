#include <ultra64.h>
#include "model.h"
#include "initactorpropstuff.h"

//uncomment when actor is worked on
//#include "chr.h"
/* defined in chr.c; used as the model vertex allocator */
extern struct Vertex *get_ptr_allocated_block_for_vertices(s32 numvertices);

void init_weapon_animation_groups_maybe(void) {
    set_vtxallocator(get_ptr_allocated_block_for_vertices);
    initWeaponAnimGroups();
}
