#include <ultra64.h>
#include <memp.h>
#include "initanitable.h"
#include "objecthandler.h"
#include "bondgame.h"
#include "initunk_0009E0.h"
#include "ramrom.h"

//bss

// Where animation frames are saved. Can possibly hold as much as nine, but the game will ever store four at maximum.
char animations_frame_buffer[0x2D0];

// Msg Queue stuff (unused)
OSMesgQueue animMsgQ;
char dword_CODE_bss_80069458[0xC0]; // Unused. Possibly meant for unused message queue.
OSMesg animMesg[8];

// Animation table ptr
struct animation_table_data * ptr_animation_table;

//data
struct bondstruct_unk_animation_related D_80029D60 = {
    NULL,
    &animations_frame_buffer, // Two pointers. One always points to the start of the buffer, the other can be modified.
    &animations_frame_buffer
};

/* Segment offsets until expand_ani_table_entries turns them into pointers. */
#define ANIM_OFS(x) ((struct ModelAnimation *)(uintptr_t)(x))

struct ModelAnimation *animation_table_ptrs1[] = {
    ANIM_OFS(PTR_ANIM_idle),
    ANIM_OFS(PTR_ANIM_fire_standing),
    ANIM_OFS(PTR_ANIM_fire_standing_fast),
    ANIM_OFS(PTR_ANIM_fire_hip),
    ANIM_OFS(PTR_ANIM_fire_shoulder_left),
    ANIM_OFS(PTR_ANIM_fire_turn_right1),
    ANIM_OFS(PTR_ANIM_fire_turn_right2),
    ANIM_OFS(PTR_ANIM_fire_kneel_right_leg),
    ANIM_OFS(PTR_ANIM_fire_kneel_left_leg),
    ANIM_OFS(PTR_ANIM_fire_kneel_left),
    ANIM_OFS(PTR_ANIM_fire_kneel_right),
    ANIM_OFS(PTR_ANIM_fire_roll_left),
    ANIM_OFS(PTR_ANIM_fire_roll_right1),
    ANIM_OFS(PTR_ANIM_fire_roll_left_fast),
    ANIM_OFS(PTR_ANIM_hit_left_shoulder),
    ANIM_OFS(PTR_ANIM_hit_right_shoulder),
    ANIM_OFS(PTR_ANIM_hit_left_arm),
    ANIM_OFS(PTR_ANIM_hit_right_arm),
    ANIM_OFS(PTR_ANIM_hit_left_hand),
    ANIM_OFS(PTR_ANIM_hit_right_hand),
    ANIM_OFS(PTR_ANIM_hit_left_leg),
    ANIM_OFS(PTR_ANIM_hit_right_leg),
    ANIM_OFS(PTR_ANIM_death_genitalia),
    ANIM_OFS(PTR_ANIM_hit_neck),
    ANIM_OFS(PTR_ANIM_death_neck),
    ANIM_OFS(PTR_ANIM_death_stagger_back_to_wall),
    ANIM_OFS(PTR_ANIM_death_forward_face_down),
    ANIM_OFS(PTR_ANIM_death_forward_spin_face_up),
    ANIM_OFS(PTR_ANIM_death_backward_fall_face_up1),
    ANIM_OFS(PTR_ANIM_death_backward_spin_face_down_right),
    ANIM_OFS(PTR_ANIM_death_backward_spin_face_up_right),
    ANIM_OFS(PTR_ANIM_death_backward_spin_face_down_left),
    ANIM_OFS(PTR_ANIM_death_backward_spin_face_up_left),
    ANIM_OFS(PTR_ANIM_death_forward_face_down_hard),
    ANIM_OFS(PTR_ANIM_death_forward_face_down_soft),
    ANIM_OFS(PTR_ANIM_death_fetal_position_right),
    ANIM_OFS(PTR_ANIM_death_fetal_position_left),
    ANIM_OFS(PTR_ANIM_death_backward_fall_face_up2),
    ANIM_OFS(PTR_ANIM_side_step_left),
    ANIM_OFS(PTR_ANIM_fire_roll_right2),
    ANIM_OFS(PTR_ANIM_walking),
    ANIM_OFS(PTR_ANIM_sprinting),
    ANIM_OFS(PTR_ANIM_running),
    ANIM_OFS(PTR_ANIM_bond_eye_walk),
    ANIM_OFS(PTR_ANIM_bond_eye_fire),
    ANIM_OFS(PTR_ANIM_bond_watch),
    ANIM_OFS(PTR_ANIM_surrendering_armed),
    ANIM_OFS(PTR_ANIM_surrendering_armed_drop_weapon),
    ANIM_OFS(PTR_ANIM_fire_walking),
    ANIM_OFS(PTR_ANIM_fire_running),
    ANIM_OFS(PTR_ANIM_null50),
    ANIM_OFS(PTR_ANIM_null51),
    ANIM_OFS(PTR_ANIM_fire_jump_to_side_left),
    ANIM_OFS(PTR_ANIM_fire_jump_to_side_right),
    ANIM_OFS(PTR_ANIM_hit_butt_long),
    ANIM_OFS(PTR_ANIM_hit_butt_short),
    ANIM_OFS(PTR_ANIM_death_head),
    ANIM_OFS(PTR_ANIM_death_left_leg),
    ANIM_OFS(PTR_ANIM_slide_right),
    ANIM_OFS(PTR_ANIM_slide_left),
    ANIM_OFS(PTR_ANIM_jump_backwards),
    ANIM_OFS(PTR_ANIM_extending_left_hand),
    ANIM_OFS(PTR_ANIM_fire_throw_grenade),
    ANIM_OFS(PTR_ANIM_spotting_bond),
    ANIM_OFS(PTR_ANIM_look_around),
    ANIM_OFS(PTR_ANIM_fire_standing_one_handed_weapon),
    ANIM_OFS(PTR_ANIM_fire_standing_draw_one_handed_weapon_fast),
    ANIM_OFS(PTR_ANIM_fire_standing_draw_one_handed_weapon_slow),
    ANIM_OFS(PTR_ANIM_fire_hip_one_handed_weapon_fast),
    ANIM_OFS(PTR_ANIM_fire_hip_one_handed_weapon_slow),
    ANIM_OFS(PTR_ANIM_fire_hip_forward_one_handed_weapon),
    ANIM_OFS(PTR_ANIM_fire_standing_right_one_handed_weapon),
    ANIM_OFS(PTR_ANIM_fire_step_right_one_handed_weapon),
    ANIM_OFS(PTR_ANIM_fire_standing_left_one_handed_weapon_slow),
    ANIM_OFS(PTR_ANIM_fire_standing_left_one_handed_weapon_fast),
    ANIM_OFS(PTR_ANIM_fire_kneel_forward_one_handed_weapon_slow),
    ANIM_OFS(PTR_ANIM_fire_kneel_forward_one_handed_weapon_fast),
    ANIM_OFS(PTR_ANIM_fire_kneel_right_one_handed_weapon_slow),
    ANIM_OFS(PTR_ANIM_fire_kneel_right_one_handed_weapon_fast),
    ANIM_OFS(PTR_ANIM_fire_kneel_left_one_handed_weapon_slow),
    ANIM_OFS(PTR_ANIM_fire_kneel_left_one_handed_weapon_fast),
    ANIM_OFS(PTR_ANIM_fire_kneel_left_one_handed_weapon),
    ANIM_OFS(PTR_ANIM_aim_walking_one_handed_weapon),
    ANIM_OFS(PTR_ANIM_aim_walking_left_one_handed_weapon),
    ANIM_OFS(PTR_ANIM_aim_walking_right_one_handed_weapon),
    ANIM_OFS(PTR_ANIM_aim_running_one_handed_weapon),
    ANIM_OFS(PTR_ANIM_aim_running_right_one_handed_weapon),
    ANIM_OFS(PTR_ANIM_aim_running_left_one_handed_weapon),
    ANIM_OFS(PTR_ANIM_aim_sprinting_one_handed_weapon),
    ANIM_OFS(PTR_ANIM_running_one_handed_weapon),
    ANIM_OFS(PTR_ANIM_sprinting_one_handed_weapon),
    ANIM_OFS(PTR_ANIM_null91),
    ANIM_OFS(PTR_ANIM_null92),
    ANIM_OFS(PTR_ANIM_null93),
    ANIM_OFS(PTR_ANIM_null94),
    ANIM_OFS(PTR_ANIM_null95),
    ANIM_OFS(PTR_ANIM_null96),
    ANIM_OFS(PTR_ANIM_draw_one_handed_weapon_and_look_around),
    ANIM_OFS(PTR_ANIM_draw_one_handed_weapon_and_stand_up),
    ANIM_OFS(PTR_ANIM_aim_one_handed_weapon_left_right),
    ANIM_OFS(PTR_ANIM_cock_one_handed_weapon_and_turn_around),
    ANIM_OFS(PTR_ANIM_holster_one_handed_weapon_and_cross_arms),
    ANIM_OFS(PTR_ANIM_cock_one_handed_weapon_turn_around_and_stand_up),
    ANIM_OFS(PTR_ANIM_draw_one_handed_weapon_and_turn_around),
    ANIM_OFS(PTR_ANIM_step_forward_and_hold_one_handed_weapon),
    ANIM_OFS(PTR_ANIM_holster_one_handed_weapon_and_adjust_suit),
    ANIM_OFS(PTR_ANIM_idle_unarmed),
    ANIM_OFS(PTR_ANIM_walking_unarmed),
    ANIM_OFS(PTR_ANIM_fire_walking_dual_wield),
    ANIM_OFS(PTR_ANIM_fire_walking_dual_wield_hands_crossed),
    ANIM_OFS(PTR_ANIM_fire_running_dual_wield),
    ANIM_OFS(PTR_ANIM_fire_running_dual_wield_hands_crossed),
    ANIM_OFS(PTR_ANIM_fire_sprinting_dual_wield),
    ANIM_OFS(PTR_ANIM_fire_sprinting_dual_wield_hands_crossed),
    ANIM_OFS(PTR_ANIM_walking_female),
    ANIM_OFS(PTR_ANIM_running_female),
    ANIM_OFS(PTR_ANIM_fire_kneel_dual_wield),
    ANIM_OFS(PTR_ANIM_fire_kneel_dual_wield_left),
    ANIM_OFS(PTR_ANIM_fire_kneel_dual_wield_right),
    ANIM_OFS(PTR_ANIM_fire_kneel_dual_wield_hands_crossed),
    ANIM_OFS(PTR_ANIM_fire_kneel_dual_wield_hands_crossed_left),
    ANIM_OFS(PTR_ANIM_fire_kneel_dual_wield_hands_crossed_right),
    ANIM_OFS(PTR_ANIM_fire_standing_dual_wield),
    ANIM_OFS(PTR_ANIM_fire_standing_dual_wield_left),
    ANIM_OFS(PTR_ANIM_fire_standing_dual_wield_right),
    ANIM_OFS(PTR_ANIM_fire_standing_dual_wield_hands_crossed_left),
    ANIM_OFS(PTR_ANIM_fire_standing_dual_wield_hands_crossed_right),
    ANIM_OFS(PTR_ANIM_fire_standing_aiming_down_sights),
    ANIM_OFS(PTR_ANIM_fire_kneel_aiming_down_sights),
    ANIM_OFS(PTR_ANIM_hit_taser),
    ANIM_OFS(PTR_ANIM_death_explosion_forward),
    ANIM_OFS(PTR_ANIM_death_explosion_left1),
    ANIM_OFS(PTR_ANIM_death_explosion_back_left),
    ANIM_OFS(PTR_ANIM_death_explosion_back1),
    ANIM_OFS(PTR_ANIM_death_explosion_right),
    ANIM_OFS(PTR_ANIM_death_explosion_forward_right1),
    ANIM_OFS(PTR_ANIM_death_explosion_back2),
    ANIM_OFS(PTR_ANIM_death_explosion_forward_roll),
    ANIM_OFS(PTR_ANIM_death_explosion_forward_face_down),
    ANIM_OFS(PTR_ANIM_death_explosion_left2),
    ANIM_OFS(PTR_ANIM_death_explosion_forward_right2),
    ANIM_OFS(PTR_ANIM_death_explosion_forward_right2_alt),
    ANIM_OFS(PTR_ANIM_death_explosion_forward_right3),
    ANIM_OFS(PTR_ANIM_null143),
    ANIM_OFS(PTR_ANIM_null144),
    ANIM_OFS(PTR_ANIM_null145),
    ANIM_OFS(PTR_ANIM_null146),
    ANIM_OFS(PTR_ANIM_running_hands_up),
    ANIM_OFS(PTR_ANIM_sprinting_hands_up),
    ANIM_OFS(PTR_ANIM_aim_and_blow_one_handed_weapon),
    ANIM_OFS(PTR_ANIM_aim_one_handed_weapon_left),
    ANIM_OFS(PTR_ANIM_aim_one_handed_weapon_right),
    ANIM_OFS(PTR_ANIM_conversation),
    ANIM_OFS(PTR_ANIM_drop_weapon_and_show_fight_stance),
    ANIM_OFS(PTR_ANIM_yawning),
    ANIM_OFS(PTR_ANIM_swatting_flies),
    ANIM_OFS(PTR_ANIM_scratching_leg),
    ANIM_OFS(PTR_ANIM_scratching_butt),
    ANIM_OFS(PTR_ANIM_adjusting_crotch),
    ANIM_OFS(PTR_ANIM_sneeze),
    ANIM_OFS(PTR_ANIM_conversation_cleaned),
    ANIM_OFS(PTR_ANIM_conversation_listener),
    ANIM_OFS(PTR_ANIM_startled_and_looking_around),
    ANIM_OFS(PTR_ANIM_laughing_in_disbelief),
    ANIM_OFS(PTR_ANIM_surrendering_unarmed),
    ANIM_OFS(PTR_ANIM_coughing_standing),
    ANIM_OFS(PTR_ANIM_coughing_kneel1),
    ANIM_OFS(PTR_ANIM_coughing_kneel2),
    ANIM_OFS(PTR_ANIM_standing_up),
    ANIM_OFS(PTR_ANIM_null169),
    ANIM_OFS(PTR_ANIM_dancing),
    ANIM_OFS(PTR_ANIM_dancing_one_handed_weapon),
    ANIM_OFS(PTR_ANIM_keyboard_right_hand1),
    ANIM_OFS(PTR_ANIM_keyboard_right_hand2),
    ANIM_OFS(PTR_ANIM_keyboard_left_hand),
    ANIM_OFS(PTR_ANIM_keyboard_right_hand_tapping),
    ANIM_OFS(PTR_ANIM_bond_eye_fire_alt),
    ANIM_OFS(PTR_ANIM_dam_jump),
    ANIM_OFS(PTR_ANIM_surface_vent_jump),
    ANIM_OFS(PTR_ANIM_cradle_jump),
    ANIM_OFS(PTR_ANIM_cradle_fall),
    ANIM_OFS(PTR_ANIM_credits_bond_kissing),
    ANIM_OFS(PTR_ANIM_credits_natalya_kissing),
    0
};

struct ModelAnimation *animation_table_ptrs2[] = {
    ANIM_OFS(PTR_ANIM_helicopter_cradle),
    ANIM_OFS(PTR_ANIM_plane_runway),
    ANIM_OFS(PTR_ANIM_helicopter_takeoff),
    0
};


extern u32 _animation_entriesSegmentRomStart[];

/* N64 layout of the packed ModelAnimation header, used in place. */
struct anim_entry
{
    s32 unk00;
    s32 unk04;
    s32 unk08;
    s32 unk0C;
    s32 unk10;
};

void expand_ani_table_entries(struct ModelAnimation **arg0)
{
    struct ModelAnimation **var_v0;

    var_v0 = arg0;
    while (*var_v0 != 0) {
        if (*var_v0 != (struct ModelAnimation *)1) {
            *var_v0 = (struct ModelAnimation *)((uintptr_t)*var_v0 + (uintptr_t)(&ptr_animation_table->data));
#ifndef MODELANIM_OFFSETS /* 64-bit keeps the header's offsets (bondtypes.h) */
            ((struct anim_entry *)*var_v0)->unk08 = ((struct anim_entry *)*var_v0)->unk08 + (uintptr_t)&ptr_animation_table->data;
            ((struct anim_entry *)*var_v0)->unk10 = ((struct anim_entry *)*var_v0)->unk10 + (uintptr_t)&ptr_animation_table->data;
#endif
        }
        var_v0++;
    }

#ifndef MODELANIM_OFFSETS
    for (var_v0 = arg0; *var_v0 != 0; var_v0++) {
        if (*var_v0 != (struct ModelAnimation *)1) {
            ((struct anim_entry *)*var_v0)->unk00 = ((struct anim_entry *)*var_v0)->unk00 + (uintptr_t)&_animation_entriesSegmentRomStart;
        }
    }
#endif
}

void alloc_load_expand_ani_table(void)
{
    s32 animsDataSegmentSize;
    
    osCreateMesgQueue(&animMsgQ, animMesg, 8);
    initAnimationsBuffer(&D_80029D60, &animMsgQ, &dword_CODE_bss_80069458);
    
    animsDataSegmentSize = (s32)((uintptr_t)&_animation_dataSegmentEnd - (uintptr_t)&_animation_dataSegmentStart);
    
    ptr_animation_table = mempAllocBytesInBank(animsDataSegmentSize, MEMPOOL_PERMANENT);

    romCopy(ptr_animation_table, &_animation_dataSegmentRomStart, animsDataSegmentSize);
    expand_ani_table_entries(animation_table_ptrs1);
    expand_ani_table_entries(animation_table_ptrs2);
}
