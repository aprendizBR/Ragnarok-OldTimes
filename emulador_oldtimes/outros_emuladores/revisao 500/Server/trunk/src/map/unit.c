// Copyright (c) jAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder
// Merged originally from jA by Skotlex
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/showmsg.h"
#include "../common/timer.h"
#include "../common/nullpo.h"
#include "../common/db.h"
#include "../common/malloc.h"
#include "unit.h"
#include "map.h"
#include "pc.h"
#include "mob.h"
#include "pet.h"
#include "skill.h"
#include "clif.h"
#include "npc.h"
#include "guild.h"
#include "status.h"
#include "battle.h"
#include "chat.h"
#include "trade.h"
#include "vending.h"
#include "party.h"
#include "intif.h"
#include "chrif.h"

static int dirx[8]={0,-1,-1,-1,0,1,1,1};
static int diry[8]={1,1,0,-1,-1,-1,0,1};

struct unit_data* unit_bl2ud(struct block_list *bl) {
	if( bl == NULL) return NULL;
	if( bl->type == BL_PC)  return &((struct map_session_data*)bl)->ud;
	if( bl->type == BL_MOB) return &((struct mob_data*)bl)->ud;
	if( bl->type == BL_PET) return &((struct pet_data*)bl)->ud;
	if( bl->type == BL_NPC) return &((struct npc_data*)bl)->ud;
	return NULL;
}

static int unit_walktoxy_timer(int tid,unsigned int tick,int id,int data);

int unit_walktoxy_sub(struct block_list *bl)
{
	int i;
	struct walkpath_data wpd;
	struct unit_data *ud = NULL;

	nullpo_retr(1, bl);
	ud = unit_bl2ud(bl);
	if(ud == NULL) return 0;

	if(path_search(&wpd,bl->m,bl->x,bl->y,ud->to_x,ud->to_y,ud->state.walk_easy))
		return 0;

	memcpy(&ud->walkpath,&wpd,sizeof(wpd));
	
	if (ud->target && ud->chaserange>1) {
		//Generally speaking, the walk path is already to an adjacent tile
		//so we only need to shorten the path if the range is greater than 1.
		int dir;
		//Trim the last part of the path to account for range,
		//but always move at least one cell when requested to move.
		for (i = ud->chaserange*10; i > 0 && ud->walkpath.path_len>1;) {
		   ud->walkpath.path_len--;
			dir = ud->walkpath.path[ud->walkpath.path_len];
			if(dir&1)
				i-=14;
			else
				i-=10;
			ud->to_x -= dirx[dir];
			ud->to_y -= diry[dir];
		}
	}

	ud->state.change_walk_target=0;

	if (bl->type == BL_PC)
		clif_walkok((TBL_PC*)bl);
	clif_move(bl);

	if(ud->walkpath.path_pos>=ud->walkpath.path_len)
		i = -1;
	else if(ud->walkpath.path[ud->walkpath.path_pos]&1)
		i = status_get_speed(bl)*14/10;
	else
		i = status_get_speed(bl);
	if( i > 0)
		ud->walktimer = add_timer(gettick()+i,unit_walktoxy_timer,bl->id,i);
	return 1;
}

static int unit_walktoxy_timer(int tid,unsigned int tick,int id,int data)
{
	int i;
	int x,y,dx,dy,dir;
	struct block_list       *bl;
	struct map_session_data *sd = NULL;
	struct mob_data         *md = NULL;
	struct unit_data        *ud = NULL;

	bl=map_id2bl(id);
	if(bl == NULL)
		return 0;
	if( BL_CAST( BL_PC,  bl, sd ) ) {
		ud = &sd->ud;
	} else if( BL_CAST( BL_MOB, bl, md ) ) {
		ud = &md->ud;
	} else
		ud = unit_bl2ud(bl);
	
	if(ud == NULL) return 0;

	if(ud->walktimer != tid){
		if(battle_config.error_log)
			ShowError("unit_walk_timer mismatch %d != %d\n",ud->walktimer,tid);
		return 0;
	}
	ud->walktimer=-1;
	if( bl->prev == NULL ) return 0; // block_list から抜けているので移動停止する

	if(ud->walkpath.path_pos>=ud->walkpath.path_len)
		return 0;

	//歩いたので息吹のタイマーを初期化
	if(sd) {
		sd->inchealspirithptick = 0;
		sd->inchealspiritsptick = 0;
	}
	
	if(ud->walkpath.path[ud->walkpath.path_pos]>=8)
		return 1;
	x = bl->x;
	y = bl->y;

	dir = ud->walkpath.path[ud->walkpath.path_pos];
	ud->dir = dir;
	if (sd) 
		sd->head_dir = dir;

	dx = dirx[(int)dir];
	dy = diry[(int)dir];

	if(map_getcell(bl->m,x+dx,y+dy,CELL_CHKNOPASS))
		return unit_walktoxy_sub(bl);
	
	// バシリカ判定

	map_foreachinmovearea(clif_outsight,bl->m,
		x-AREA_SIZE,y-AREA_SIZE,x+AREA_SIZE,y+AREA_SIZE,
		dx,dy,sd?BL_ALL:BL_PC,bl);

	x += dx;
	y += dy;
	map_moveblock(bl, x, y, tick);
	ud->walk_count++; //walked cell counter, to be used for walk-triggered skills. [Skotlex]

	ud->walktimer = 1;
	map_foreachinmovearea(clif_insight,bl->m,
		x-AREA_SIZE,y-AREA_SIZE,x+AREA_SIZE,y+AREA_SIZE,
		-dx,-dy,sd?BL_ALL:BL_PC,bl);
	ud->walktimer = -1;
	
	if(sd) {
		if(map_getcell(bl->m,x,y,CELL_CHKNPC)) {
			npc_touch_areanpc(sd,bl->m,x,y);
			if (bl->prev == NULL) //Script could have warped char, abort remaining of the function.
				return 0;
		} else
			sd->areanpc_id=0;
		if (sd->state.gmaster_flag)
		{ //Guild Aura: Likely needs to be recoded, this method seems inefficient.
			struct guild *g = sd->state.gmaster_flag;
			int skill, guildflag = 0;
			if ((skill = guild_checkskill(g, GD_LEADERSHIP)) > 0) guildflag |= skill<<12;
			if ((skill = guild_checkskill(g, GD_GLORYWOUNDS)) > 0) guildflag |= skill<<8;
			if ((skill = guild_checkskill(g, GD_SOULCOLD)) > 0) guildflag |= skill<<4;
			if ((skill = guild_checkskill(g, GD_HAWKEYES)) > 0) guildflag |= skill;
			if (guildflag)
				map_foreachinrange(skill_guildaura_sub, bl,2, BL_PC,
					bl->id, sd->status.guild_id, &guildflag);
		}
		if (
			(sd->class_&MAPID_UPPERMASK) == MAPID_STAR_GLADIATOR &&
			sd->sc.data[SC_MIRACLE].timer==-1 &&
			!(ud->walk_count%WALK_SKILL_INTERVAL) &&
			rand()%10000 < battle_config.sg_miracle_skill_ratio
		) {	//SG_MIRACLE [Komurka]
			clif_displaymessage(sd->fd,"[Miracle of the Sun, Moon and Stars]");
			sc_start(&sd->bl,SC_MIRACLE,100,1,battle_config.sg_miracle_skill_duration);
		}
	} else if (md) {
		if (md->min_chase > md->db->range2) md->min_chase--;
		//Walk skills are triggered regardless of target due to the idle-walk mob state.
		if(!(ud->walk_count%WALK_SKILL_INTERVAL) &&
			mobskill_use(md, tick, -1))
			return 0;
	}

	if(tid == -1) //A directly invoked timer is from battle_stop_walking, therefore the rest is irrelevant.
		return 0;
		
	if(ud->state.change_walk_target)
		return unit_walktoxy_sub(bl);

	ud->walkpath.path_pos++;
	if(ud->walkpath.path_pos>=ud->walkpath.path_len)
		i = -1;
	else if(ud->walkpath.path[ud->walkpath.path_pos]&1)
		i = status_get_speed(bl)*14/10;
	else
		i = status_get_speed(bl);

	if(i > 0)
		ud->walktimer = add_timer(tick+i,unit_walktoxy_timer,id,i);
	else if(ud->state.running) {
		//Keep trying to run.
		if (!unit_run(bl))
			ud->state.running = 0;
	} else if (ud->target) {
		//Update target trajectory.
		struct block_list *tbl = map_id2bl(ud->target);
		if (!tbl) {	//Cancel chase.
			ud->to_x = bl->x;
			ud->to_y = bl->y;
			return 0;
		}
		if (tbl->m == bl->m && check_distance_bl(bl, tbl, ud->chaserange))
		{	//Reached destination.
			if (ud->state.attack_continue)
				unit_attack(bl, tbl->id, ud->state.attack_continue);
		} else { //Update chase-path
			unit_walktobl(bl, tbl, ud->chaserange, ud->state.walk_easy|(ud->state.attack_continue?2:0));
			return 0;
		}
	} else {	//Stopped walking. Update to_x and to_y to current location [Skotlex]
		ud->to_x = bl->x;
		ud->to_y = bl->y;
//		if (bl->type == BL_NPC) //Original eA code had this one only for BL_NPCs
//			clif_fixpos(bl);
	}
	return 0;
}

int unit_walktoxy( struct block_list *bl, int x, int y, int easy) {
	struct unit_data        *ud = NULL;
	struct status_change		*sc = NULL;

	nullpo_retr(0, bl);
	
	ud = unit_bl2ud(bl);
	
	if( ud == NULL) return 0;

	// 移動出来ないユニットは弾く
	if(!(status_get_mode(bl)&MD_CANMOVE) || !unit_can_move(bl))
		return 0;

	ud->state.walk_easy = easy;
	ud->target = 0;
	ud->to_x = x;
	ud->to_y = y;
	
	sc = status_get_sc(bl);
	if (sc && sc->count && sc->data[SC_CONFUSION].timer != -1) //Randomize the target position
		map_random_dir(bl, &ud->to_x, &ud->to_y);

	if(ud->walktimer != -1) {
		// 現在歩いている最中の目的地変更なのでマス目の中心に来た時に
		// timer関数からunit_walktoxy_subを呼ぶようにする
		ud->state.change_walk_target = 1;
		return 1;
	} else {
		return unit_walktoxy_sub(bl);
	}
}

static int unit_walktobl_sub(int tid,unsigned int tick,int id,int data)
{
	struct block_list *bl = map_id2bl(id);
	struct unit_data *ud = bl?unit_bl2ud(bl):NULL;

	if (ud && ud->walktimer == -1 && ud->target == data)
	{
		if (DIFF_TICK(ud->canmove_tick, tick) > 0) //Keep waiting?
			add_timer(ud->canmove_tick+1, unit_walktobl_sub, id, data);
		else if (unit_can_move(bl))
			unit_walktoxy_sub(bl);
	}
	return 0;	
}

// Chases a tbl. If the flag&1, use hard-path seek,
// if flag&2, start attacking upon arrival within range, otherwise just walk to that character.
int unit_walktobl(struct block_list *bl, struct block_list *tbl, int range, int flag) {
	struct unit_data        *ud = NULL;
	struct status_change		*sc = NULL;
	nullpo_retr(0, bl);
	nullpo_retr(0, tbl);
	
	ud = unit_bl2ud(bl);
	if( ud == NULL) return 0;

	if (!(status_get_mode(bl)&MD_CANMOVE))
		return 0;
	
	if (!unit_can_reach_bl(bl, tbl, distance_bl(bl, tbl)+1, flag&1, &ud->to_x, &ud->to_y)) {
		ud->to_x = bl->x;
		ud->to_y = bl->y;
		return 0;
	}

	ud->state.walk_easy = flag&1;
	ud->target = tbl->id;
	ud->chaserange = range; //Note that if flag&2, this SHOULD be attack-range
	ud->state.attack_continue = flag&2?1:0; //Chase to attack.

	sc = status_get_sc(bl);
	if (sc && sc->count && sc->data[SC_CONFUSION].timer != -1) //Randomize the target position
		map_random_dir(bl, &ud->to_x, &ud->to_y);

	
	if(ud->walktimer != -1) {
		ud->state.change_walk_target = 1;
		return 1;
	}
	if (DIFF_TICK(ud->canmove_tick, gettick()) > 0)
	{	//Can't move, wait a bit before invoking the movement.
		add_timer(ud->canmove_tick+1, unit_walktobl_sub, bl->id, ud->target);
		return 1;
	} else if (!unit_can_move(bl))
		return 0;
	
	return unit_walktoxy_sub(bl);
}

int unit_run(struct block_list *bl)
{
	struct status_change *sc = status_get_sc(bl);
	int i,to_x,to_y,dir_x,dir_y;

	if (!sc || !sc->count || sc->data[SC_RUN].timer == -1)
		return 0;
	
	if (!unit_can_move(bl)) {
		if(sc->data[SC_RUN].timer!=-1)
			status_change_end(bl,SC_RUN,-1);
		return 0;
	}
	
	to_x = bl->x;
	to_y = bl->y;
	dir_x = dirx[sc->data[SC_RUN].val2];
	dir_y = diry[sc->data[SC_RUN].val2];

	for(i=0;i<AREA_SIZE;i++)
	{
		if(!map_getcell(bl->m,to_x+dir_x,to_y+dir_y,CELL_CHKPASS))
			break;
		to_x += dir_x;
		to_y += dir_y;
	}

	if(to_x == bl->x && to_y == bl->y) {
		status_change_end(bl,SC_RUN,-1);
		return 0;
	}
	unit_walktoxy(bl, to_x, to_y, 1);
	return 1;
}

//Instant warp function.
int unit_movepos(struct block_list *bl,int dst_x,int dst_y, int easy, int checkpath)
{
	int dx,dy,dir;
	struct unit_data        *ud = NULL;
	struct map_session_data *sd = NULL;
	struct walkpath_data wpd;

	nullpo_retr(0, bl);
	if( BL_CAST( BL_PC,  bl, sd ) ) {
		ud = &sd->ud;
	} else
		ud = unit_bl2ud(bl);
	
	if( ud == NULL) return 0;

	unit_stop_walking(bl,1);
	unit_stop_attack(bl);

	if(checkpath && (map_getcell(bl->m,bl->x,bl->y, CELL_CHKNOPASS) || path_search_real(&wpd,bl->m,bl->x,bl->y,dst_x,dst_y,easy, CELL_CHKNOREACH)))
		return 0;

	dir = map_calc_dir(bl, dst_x,dst_y);
	ud->dir = dir;
	if(sd) sd->head_dir = dir;

	dx = dst_x - bl->x;
	dy = dst_y - bl->y;

	map_foreachinmovearea(clif_outsight,bl->m,
		bl->x-AREA_SIZE,bl->y-AREA_SIZE,bl->x+AREA_SIZE,bl->y+AREA_SIZE,
		dx,dy,sd?BL_ALL:BL_PC,bl);

	map_moveblock(bl, dst_x, dst_y, gettick());
	
	ud->walktimer = 1;
	map_foreachinmovearea(clif_insight,bl->m,
		bl->x-AREA_SIZE,bl->y-AREA_SIZE,bl->x+AREA_SIZE,bl->y+AREA_SIZE,
		-dx,-dy,sd?BL_ALL:BL_PC,bl);
	ud->walktimer = -1;
		
	if(sd) {
		if(map_getcell(bl->m,bl->x,bl->y,CELL_CHKNPC)) {
			npc_touch_areanpc(sd,bl->m,bl->x,bl->y);
			if (bl->prev == NULL) //Script could have warped char, abort remaining of the function.
				return 0;
		} else
			sd->areanpc_id=0;
		if(sd->status.pet_id > 0 && sd->pd && sd->pet.intimate > 0)
		{	//Check if pet needs to be teleported. [Skotlex]
			int flag = 0;
			bl = &sd->pd->bl; //Note that bl now points to the pet! 
			if (!checkpath && path_search(&wpd,bl->m,bl->x,bl->y,dst_x,dst_y,0))
				flag = 1;
			else if (!check_distance_bl(&sd->bl, bl, AREA_SIZE)) //Too far, teleport.
				flag = 2;
			if (flag) {
				unit_movepos(bl,sd->bl.x,sd->bl.y, 0, 0);
				clif_slide(bl,bl->x,bl->y);
			}
		//If you want to use bl afterwards, uncomment this:
		//bl = &sd->bl;
		}
	}
	return 1;
}

int unit_setdir(struct block_list *bl,unsigned char dir)
{
	struct unit_data *ud;
	nullpo_retr( 0, bl );
	ud = unit_bl2ud(bl);
	if (!ud) return 0;
	ud->dir = dir;
	if (bl->type == BL_PC) 
		((TBL_PC *)bl)->head_dir = dir;
	clif_changed_dir(bl);
	return 0;
}

int unit_getdir(struct block_list *bl)
{
	struct unit_data *ud;
	nullpo_retr( 0, bl );
	ud = unit_bl2ud(bl);
	if (!ud) return 0;
	return ud->dir;
}

//Warps a unit/ud to a given map/position. 
//In the case of players, pc_setpos is used.
//it respects the no warp flags, so it is safe to call this without doing nowarpto/nowarp checks.
int unit_warp(struct block_list *bl,int m,short x,short y,int type)
{
	struct unit_data *ud;
	nullpo_retr(0, bl);
	ud = unit_bl2ud(bl);
	
	if(bl->prev==NULL || !ud)
		return 1;

	if (type < 0 || type == 1)
		//Type 1 is invalid, since you shouldn't warp a bl with the "death" 
		//animation, it messes up with unit_remove_map! [Skotlex]
		return 1;
	
	if( m<0 ) m=bl->m;
	
	switch (bl->type) {
		case BL_MOB:
			if (map[bl->m].flag.monster_noteleport)
				return 1;
			break;
		case BL_PC:
			if (map[bl->m].flag.noteleport)
				return 1;
			break;
	}
	
	if (x<0 || y<0)
  	{	//Random map position.
		if (!map_search_freecell(NULL, m, &x, &y, -1, -1, 1)) {
			if(battle_config.error_log)
				ShowWarning("unit_warp failed. Unit Id:%d/Type:%d, target position map %d (%s) at [%d,%d]\n", bl->id, bl->type, m, map[m].name, x, y);
			return 2;
			
		}
	} else if (map_getcell(m,x,y,CELL_CHKNOREACH))
	{	//Invalid target cell
		if(battle_config.error_log)
			ShowWarning("unit_warp: Specified non-walkable target cell: %d (%s) at [%d,%d]\n", m, map[m].name, x,y);
		
		if (!map_search_freecell(NULL, m, &x, &y, 4, 4, 1))
	 	{	//Can't find a nearby cell
			if(battle_config.error_log)
				ShowWarning("unit_warp failed. Unit Id:%d/Type:%d, target position map %d (%s) at [%d,%d]\n", bl->id, bl->type, m, map[m].name, x, y);
			return 2;
		}
	}
			
	if (bl->type == BL_PC) //Use pc_setpos
		return pc_setpos((TBL_PC*)bl, map[m].index, x, y, type);
	
	if (!unit_remove_map(bl, type))
		return 3;
	
	bl->x=ud->to_x=x;
	bl->y=ud->to_y=y;
	bl->m=m;

	map_addblock(bl);
	clif_spawn(bl);
//This is broken because the mob already was changed from map.
//Fortunately, the slave ai will make them chase their master automatically
//	if (bl->type == BL_MOB)
//		mob_warpslave(bl,AREA_SIZE);
	skill_unit_move(bl,gettick(),1);
	return 0;
}

/*==========================================
 * 歩行停止
 *------------------------------------------
 */
int unit_stop_walking(struct block_list *bl,int type)
{
	struct unit_data *ud;
	struct TimerData *data;
	unsigned int tick;
	nullpo_retr(0, bl);

	ud = unit_bl2ud(bl);
	if(!ud || ud->walktimer == -1)
		return 0;
	//NOTE: We are using timer data after deleting it because we know the 
	//delete_timer function does not messes with it. If the function's 
	//behaviour changes in the future, this code could break!
	data = get_timer(ud->walktimer);
	delete_timer(ud->walktimer, unit_walktoxy_timer);
	ud->walktimer = -1;
	ud->state.change_walk_target = 0;
	tick = gettick();
	if ((type&0x02 && !ud->walkpath.path_pos) //Force moving at least one cell.
		|| (data && DIFF_TICK(data->tick, tick) <= data->data/2)) //Enough time has passed to cover half-cell
	{	
		ud->walkpath.path_len = ud->walkpath.path_pos+1;
		unit_walktoxy_timer(-1, tick, bl->id, ud->walkpath.path_pos);
	}

//	if(md) { md->state.skillstate = MSS_IDLE; }
	if(type&0x01)
		clif_fixpos(bl);
	
	ud->walkpath.path_len = 0;
	ud->walkpath.path_pos = 0;
	ud->to_x = bl->x;
	ud->to_y = bl->y;
	if(bl->type == BL_PET && type&~0xff)
		ud->canmove_tick = gettick() + (type>>8);

	if (ud->state.running)
		status_change_end(bl, SC_RUN, -1);
	return 1;
}

int unit_skilluse_id(struct block_list *src, int target_id, int skill_num, int skill_lv) {

	if(skill_num < 0) return 0;

	return unit_skilluse_id2(
		src, target_id, skill_num, skill_lv,
		skill_castfix(src, skill_num, skill_lv),
		skill_get_castcancel(skill_num)
	);
}

int unit_is_walking(struct block_list *bl)
{
	struct unit_data *ud = unit_bl2ud(bl);
	nullpo_retr(0, bl);
	if(!ud) return 0;
	return (ud->walktimer != -1);
}

/*==========================================
 * Determines if the bl can move based on status changes. [Skotlex]
 *------------------------------------------
 */
int unit_can_move(struct block_list *bl)
{
	struct map_session_data *sd;
	struct unit_data *ud;
	struct status_change *sc;
	
	nullpo_retr(0, bl);
	ud = unit_bl2ud(bl);
	sc = status_get_sc(bl);
	BL_CAST(BL_PC, bl, sd);
	
	if (!ud)
		return 0;
	
	if (ud->skilltimer != -1 && (!sd || pc_checkskill(sd, SA_FREECAST) <= 0))
		return 0;
		
	if (DIFF_TICK(ud->canmove_tick, gettick()) > 0)
		return 0;

	if (sd && pc_issit(sd))
		return 0; //Can't move
	
	if (sc) {
		if (sc->opt1 > 0 && sc->opt1 != OPT1_STONEWAIT)
			return 0;

		if ((sc->option & OPTION_HIDE) && (!sd || pc_checkskill(sd, RG_TUNNELDRIVE) <= 0))
			return 0;

		if (sc->count && (
			sc->data[SC_ANKLE].timer != -1 ||
			sc->data[SC_AUTOCOUNTER].timer !=-1 ||
			sc->data[SC_TRICKDEAD].timer !=-1 ||
			sc->data[SC_BLADESTOP].timer !=-1 ||
			sc->data[SC_SPIDERWEB].timer !=-1 ||
			(sc->data[SC_DANCING].timer !=-1 && (
				(sc->data[SC_DANCING].val4 && sc->data[SC_LONGING].timer == -1) ||
				sc->data[SC_DANCING].val1 == CG_HERMODE	//cannot move while Hermod is active.
			)) ||
			(sc->data[SC_GOSPEL].timer !=-1 && sc->data[SC_GOSPEL].val4 == BCT_SELF) ||	// cannot move while gospel is in effect
			sc->data[SC_STOP].timer != -1 ||
			sc->data[SC_CLOSECONFINE].timer != -1 ||
			sc->data[SC_CLOSECONFINE2].timer != -1
		))
			return 0;
	}
	return 1;
}

/*==========================================
 * Applies walk delay to character, considering that 
 * if type is 0, this is a damage induced delay: if previous delay is active, do not change it.
 * if type is 1, this is a skill induced delay: walk-delay may only be increased, not decreased.
 *------------------------------------------
 */
int unit_set_walkdelay(struct block_list *bl, unsigned int tick, int delay, int type)
{
	struct unit_data *ud = unit_bl2ud(bl);
	if (delay <= 0 || !ud) return 0;
	
	if (type) {
		if (DIFF_TICK(ud->canmove_tick, tick+delay) > 0)
			return 0;
	} else {
		if (DIFF_TICK(ud->canmove_tick, tick) > 0)
			return 0;
	}
	ud->canmove_tick = tick + delay;
	if (ud->walktimer != -1)
	{	//Stop walking, if chasing, readjust timers.
		unit_stop_walking(bl,3);
		if(ud->target)
			add_timer(ud->canmove_tick+1, unit_walktobl_sub, bl->id, ud->target);
	}
	return 1;
}

/*==========================================
 * Applies walk delay based on attack type. [Skotlex]
 *------------------------------------------
 */

int unit_skilluse_id2(struct block_list *src, int target_id, int skill_num, int skill_lv, int casttime, int castcancel) {
	struct unit_data *ud;
	struct status_change *sc;
	struct map_session_data *sd = NULL;
	struct block_list * target = NULL;
	unsigned int tick = gettick();
	int temp;

	nullpo_retr(0, src);
	if(status_isdead(src))
		return 0; // 死んでいないか

	if( BL_CAST( BL_PC,  src, sd ) ) {
		ud = &sd->ud;
	} else
		ud = unit_bl2ud(src);

	if(ud == NULL) return 0;
	sc = status_get_sc(src);	
	if (sc && !sc->count)
		sc = NULL; //Unneeded
	//temp: used to signal combo-skills right now.
	temp = (target_id == src->id && !(sd && sd->state.skill_flag)
		&& skill_get_inf(skill_num)&INF_SELF_SKILL
		&& skill_get_inf2(skill_num)&INF2_NO_TARGET_SELF);
	if (temp)
		target_id = ud->target; //Auto-select skills. [Skotlex]

	if (sd) {
		//Target_id checking.
		if(skillnotok(skill_num, sd)) // [MouseJstr]
			return 0;
		switch(skill_num)
		{	//Check for skills that auto-select target
		case MO_CHAINCOMBO:
			if (sc && sc->data[SC_BLADESTOP].timer != -1){
				if ((target=(struct block_list *)sc->data[SC_BLADESTOP].val4) == NULL)
					return 0;
			}
			break;
		case TK_JUMPKICK:
		case TK_COUNTER:
		case HT_POWER:
			if (sc && sc->data[SC_COMBO].timer != -1 && sc->data[SC_COMBO].val1 == skill_num)
				target_id = sc->data[SC_COMBO].val2;
			break;
		case WE_MALE:
		case WE_FEMALE:
			if (!sd->status.partner_id)
				return 0;
			target = (struct block_list*)map_charid2sd(sd->status.partner_id);
			if (!target) {
				clif_skill_fail(sd,skill_num,0,0);
				return 0;
			}
			break;
		}
		if (target)
			target_id = target->id;
	}
	if(!target && (target=map_id2bl(target_id)) == NULL )
		return 0;
	if(src->m != target->m)
		return 0; // 同じマップかどうか
	if(!src->prev || !target->prev)
		return 0; // map 上に存在するか

	//Normally not needed because clif.c checks for it, but the at/char/script commands don't! [Skotlex]
	if(ud->skilltimer != -1 && skill_num != SA_CASTCANCEL)
		return 0;

	if(skill_get_inf2(skill_num)&INF2_NO_TARGET_SELF && src->id == target_id)
		return 0;

	if(!status_check_skilluse(src, target, skill_num, 0))
		return 0;

	//直前のスキル状況の記録
	if(sd) {
		switch(skill_num){
		case SA_CASTCANCEL:
			if(ud->skillid != skill_num){ //キャストキャンセル自体は覚えない
				sd->skillid_old = ud->skillid;
				sd->skilllv_old = ud->skilllv;
				break;
			}
		case BD_ENCORE:					/* アンコール */
			//Prevent using the dance skill if you no longer have the skill in your tree. 
			if(!sd->skillid_dance || pc_checkskill(sd,sd->skillid_dance)<=0){
				clif_skill_fail(sd,skill_num,0,0);
				return 0;
			}
			sd->skillid_old = skill_num;
			break;
		case BD_LULLABY:				/* 子守歌 */
		case BD_RICHMANKIM:				/* ニヨルドの宴 */
		case BD_ETERNALCHAOS:			/* 永遠の?ｬ沌 */
		case BD_DRUMBATTLEFIELD:		/* ?太鼓の響き */
		case BD_RINGNIBELUNGEN:			/* ニ?ベルングの指輪 */
		case BD_ROKISWEIL:				/* �?キの叫び */
		case BD_INTOABYSS:				/* ?[淵の中に */
		case BD_SIEGFRIED:				/* 不死?gのジ?クフリ?ド */
		case CG_MOONLIT:				/* 月明りの?�に落ちる花びら */
			if (battle_config.player_skill_partner_check &&
				(!battle_config.gm_skilluncond || pc_isGM(sd) < battle_config.gm_skilluncond) &&
				(skill_check_pc_partner(sd, skill_num, &skill_lv, 1, 0) < 1)
			) {
				clif_skill_fail(sd,skill_num,0,0);
				return 0;
			}
			break;
		}
		if (!skill_check_condition(sd, skill_num, skill_lv, 0))
			return 0;	
	}
	//TODO: Add type-independant skill_check_condition function.
	if (src->type == BL_MOB) {
		switch (skill_num) {
			case NPC_SUMMONSLAVE:
			case NPC_SUMMONMONSTER:
				if (((TBL_MOB*)src)->master_id)
					return 0;
		}
	}

	if(src->id != target_id &&
		!battle_check_range(src,target,skill_get_range2(src, skill_num,skill_lv)
		+(skill_num==RG_CLOSECONFINE?0:1))) //Close confine is exploitable thanks to this extra range "feature" of the client. [Skotlex]
		return 0;

	if (!temp) //Stop attack on non-combo skills [Skotlex]
		unit_stop_attack(src);
	else if(ud->attacktimer != -1) //Elsewise, delay current attack sequence
		ud->attackabletime = tick + status_get_adelay(src);
	
	ud->state.skillcastcancel = castcancel;

	//temp: Used to signal force cast now.
	temp = 0;
	/* 何か特殊な処理が必要 */
	// 失敗判定はskill_check_condition() に書くこと
	switch(skill_num){
	case ALL_RESURRECTION:	/* リザレクション */
		if(battle_check_undead(status_get_race(target),status_get_elem_type(target))){	/* 敵がアンデッドなら */
			temp=1;	/* ターンアンデットと同じ詠唱時間 */
			casttime = skill_castfix(src, PR_TURNUNDEAD, skill_lv);
		}
		break;
	case MO_FINGEROFFENSIVE:	/* 指弾 */
		if(sd)
			casttime += casttime * ((skill_lv > sd->spiritball)? sd->spiritball:skill_lv);
		break;
	case MO_EXTREMITYFIST:	/*阿?C羅覇鳳�?*/
		if (sc && sc->data[SC_COMBO].timer != -1 &&
			(sc->data[SC_COMBO].val1 == MO_COMBOFINISH ||
			sc->data[SC_COMBO].val1 == CH_TIGERFIST ||
			sc->data[SC_COMBO].val1 == CH_CHAINCRUSH))
			casttime = 0;
		temp = 1;
		break;
	case TK_RUN:
		if (sc && sc->data[SC_RUN].timer != -1)
			casttime = 0;
		break;
	case SA_MAGICROD:
	case SA_SPELLBREAKER:
		temp =1;
		break;
	case KN_CHARGEATK:			//チャージアタック
		//Taken from jA: Casttime is increased by dist/3*100%
		casttime = casttime * ((distance_bl(src,target)-1)/3+1);
		break;
	}

	//メモライズ状態ならキャストタイムが1/2
	if (sc && sc->data[SC_MEMORIZE].timer != -1 && casttime > 0) {
		casttime = casttime/2;
		if ((--sc->data[SC_MEMORIZE].val2) <= 0)
			status_change_end(src, SC_MEMORIZE, -1);
	}

	if( casttime>0 || temp){ /* 詠唱が必要 */

		clif_skillcasting(src, src->id, target_id, 0,0, skill_num,casttime);

		/* 詠唱反応モンスター */
		if (sd && target->type == BL_MOB)
		{
			TBL_MOB *md = (TBL_MOB*)target;
			mobskill_event(md, src, tick, -1); //Cast targetted skill event.
			//temp: used to store mob's mode now.
			if ((temp=status_get_mode(target))&MD_CASTSENSOR &&
				battle_check_target(target, src, BCT_ENEMY) > 0)
			{
				switch (md->state.skillstate) {
				case MSS_ANGRY:
				case MSS_RUSH:
				case MSS_FOLLOW:
					if (!(temp&(MD_AGGRESSIVE|MD_ANGRY)))
						break; //Only Aggressive mobs change target while chasing.
				case MSS_IDLE:
				case MSS_WALK:
					md->target_id = src->id;
					md->state.aggressive = (temp&MD_ANGRY)?1:0;
					md->min_chase = md->db->range3;
				}
			}
		}
	}

	if( casttime<=0 )
		ud->state.skillcastcancel=0;

	ud->canact_tick  = tick + casttime + 100;
	ud->skilltarget  = target_id;
	ud->skillx       = 0;
	ud->skilly       = 0;
	ud->skillid      = skill_num;
	ud->skilllv      = skill_lv;

 	if(sc && sc->data[SC_CLOAKING].timer != -1 && !sc->data[SC_CLOAKING].val4 && skill_num != AS_CLOAKING)
		status_change_end(src,SC_CLOAKING,-1);

	if(casttime > 0) {
		ud->skilltimer = add_timer( tick+casttime, skill_castend_id, src->id, 0 );
		//temp: used to hold FreeCast's level
		if(sd && (temp = pc_checkskill(sd,SA_FREECAST)) > 0)
			status_quick_recalc_speed (sd, SA_FREECAST, temp, 1);
		else
			unit_stop_walking(src,1);
	}
	else {
//		if(skill_num != SA_CASTCANCEL)
//			ud->skilltimer = -1; //This check is done above...
		skill_castend_id(ud->skilltimer,tick,src->id,0);
	}
	return 1;
}

int unit_skilluse_pos(struct block_list *src, int skill_x, int skill_y, int skill_num, int skill_lv) {
	if(skill_num < 0)
		return 0;
	return unit_skilluse_pos2(
		src, skill_x, skill_y, skill_num, skill_lv,
		skill_castfix(src, skill_num, skill_lv),
		skill_get_castcancel(skill_num)
	);
}

int unit_skilluse_pos2( struct block_list *src, int skill_x, int skill_y, int skill_num, int skill_lv, int casttime, int castcancel) {
	struct map_session_data *sd = NULL;
	struct unit_data        *ud = NULL;
	struct status_change *sc;
	struct block_list    bl;
	unsigned int tick = gettick();

	nullpo_retr(0, src);

	if(!src->prev) return 0; // map 上に存在するか
	if(status_isdead(src)) return 0;

	if( BL_CAST( BL_PC, src, sd ) ) {
		ud = &sd->ud;
	} else
		ud = unit_bl2ud(src);
	if(ud == NULL) return 0;

	if(ud->skilltimer != -1) //Normally not needed since clif.c checks for it, but at/char/script commands don't! [Skotlex]
		return 0;
	
	sc = status_get_sc(src);
	if (sc && !sc->count)
		sc = NULL;
	
	if(sd) {
		if (skillnotok(skill_num, sd) ||
			!skill_check_condition(sd, skill_num, skill_lv,0))
		return 0;
	} 
	
	if (!status_check_skilluse(src, NULL, skill_num, 0))
		return 0;

	if (map_getcell(src->m, skill_x, skill_y, CELL_CHKNOREACH))
	{	//prevent casting ground targeted spells on non-walkable areas. [Skotlex] 
		if (sd) clif_skill_fail(sd,skill_num,0,0);
		return 0;
	}

	/* 射程と障害物チェック */
	bl.type = BL_NUL;
	bl.m = src->m;
	bl.x = skill_x;
	bl.y = skill_y;
	if(skill_num != TK_HIGHJUMP &&
		!battle_check_range(src,&bl,skill_get_range2(src, skill_num,skill_lv)+1))
		return 0;

	unit_stop_attack(src);
	ud->state.skillcastcancel = castcancel;

	//�?モライズ?態ならキャストタイムが1/3
	if (sc && sc->data[SC_MEMORIZE].timer != -1 && casttime > 0){
		casttime = casttime/3;
		if ((--sc->data[SC_MEMORIZE].val2)<=0)
			status_change_end(src, SC_MEMORIZE, -1);
	}

	if( casttime>0 ) {
		/* 詠唱が必要 */
		unit_stop_walking( src, 1);		// 歩行停止
		clif_skillcasting(src, src->id, 0, skill_x,skill_y, skill_num,casttime);
	}

	if( casttime<=0 )	/* 詠唱の無いものはキャンセルされない */
		ud->state.skillcastcancel=0;

	ud->canact_tick  = tick + casttime + 100;
	ud->skillid      = skill_num;
	ud->skilllv      = skill_lv;
	ud->skillx       = skill_x;
	ud->skilly       = skill_y;
	ud->skilltarget  = 0;

	if (sc && sc->data[SC_CLOAKING].timer != -1 && !sc->data[SC_CLOAKING].val4)
		status_change_end(src,SC_CLOAKING,-1);

	if(casttime > 0) {
		ud->skilltimer = add_timer( tick+casttime, skill_castend_pos, src->id, 0 );
		//castcancel recylced to store FREECAST lv.
		if(sd && (castcancel = pc_checkskill(sd,SA_FREECAST)) > 0)
			status_quick_recalc_speed (sd, SA_FREECAST, castcancel, 1);
		else
			unit_stop_walking(src,1);
	}
	else {
		ud->skilltimer = -1;
		skill_castend_pos(ud->skilltimer,tick,src->id,0);
	}
	return 1;
}

static int unit_attack_timer(int tid,unsigned int tick,int id,int data);

// 攻撃停止
int unit_stop_attack(struct block_list *bl)
{
	struct unit_data *ud = unit_bl2ud(bl);
	nullpo_retr(0, bl);

	if(!ud || ud->attacktimer == -1)
		return 0;

	delete_timer( ud->attacktimer, unit_attack_timer );
	ud->attacktimer = -1;
	return 0;
}

//Means current target is unattackable. For now only unlocks mobs.
int unit_unattackable(struct block_list *bl) {
	struct unit_data *ud = unit_bl2ud(bl);
	if (ud) {
		ud->target = 0;
		ud->state.attack_continue = 0;
	}
	
	if(bl->type == BL_MOB)
		mob_unlocktarget((struct mob_data*)bl, gettick()) ;
	else if(bl->type == BL_PET)
		pet_unlocktarget((struct pet_data*)bl);
	return 0;
}

/*==========================================
 * 攻撃要求
 * typeが1なら継続攻撃
 *------------------------------------------
 */

int unit_attack(struct block_list *src,int target_id,int type)
{
	struct block_list *target;
	struct unit_data  *ud;

	nullpo_retr(0, ud = unit_bl2ud(src));

	target=map_id2bl(target_id);
	if(target==NULL || status_isdead(target)) {
		unit_unattackable(src);
		return 1;
	}

	if(src->type == BL_PC && target->type==BL_NPC) { // monster npcs [Valaris]
		npc_click((TBL_PC*)src,target_id); // submitted by leinsirk10 [Celest]
		return 0;
	}

	if(battle_check_target(src,target,BCT_ENEMY)<=0 ||
		!status_check_skilluse(src, target, 0, 0)
	) {
		unit_unattackable(src);
		return 1;
	}

	ud->target = target_id;
	ud->state.attack_continue = type;
	if (type) //If you re to attack continously, set to auto-case character
		ud->chaserange = status_get_range(src);
	
	//Just change target/type. [Skotlex]
	if(ud->attacktimer != -1)
		return 0;

	if(DIFF_TICK(ud->attackabletime, gettick()) > 0)
		//Do attack next time it is possible. [Skotlex]
		ud->attacktimer=add_timer(ud->attackabletime,unit_attack_timer,src->id,0);
	else //Attack NOW.
		unit_attack_timer(-1,gettick(),src->id,0);

	return 0;
}

/*==========================================
 *
 *------------------------------------------
 */
int unit_can_reach_pos(struct block_list *bl,int x,int y, int easy)
{
	struct walkpath_data wpd;

	nullpo_retr(0, bl);

	if( bl->x==x && bl->y==y )	// 同じマス
		return 1;

	// 障害物判定
	wpd.path_len=0;
	wpd.path_pos=0;
	wpd.path_half=0;
	return (path_search_real(&wpd,bl->m,bl->x,bl->y,x,y,easy,CELL_CHKNOREACH)!=-1);
}

/*==========================================
 *
 *------------------------------------------
 */
int unit_can_reach_bl(struct block_list *bl,struct block_list *tbl, int range, int easy, short *x, short *y)
{
	struct walkpath_data wpd;
	int i;
	short dx,dy;
	nullpo_retr(0, bl);
	nullpo_retr(0, tbl);

	if( bl->m != tbl->m)
		return 0;
	
	if( bl->x==tbl->x && bl->y==tbl->y )
		return 1;

	if(range>0 && !check_distance_bl(bl, tbl, range))
		return 0;

	wpd.path_len=0;
	wpd.path_pos=0;
	wpd.path_half=0;
	
	// It judges whether it can adjoin or not.
	dx=tbl->x - bl->x;
	dy=tbl->y - bl->y;
	dx=(dx>0)?1:((dx<0)?-1:0);
	dy=(dy>0)?1:((dy<0)?-1:0);
	
	if (map_getcell(tbl->m,tbl->x+dx,tbl->y+dy,CELL_CHKNOREACH))
	{	//Look for a suitable cell to place in.
		for(i=0;i<9 && map_getcell(tbl->m,tbl->x-dirx[i],tbl->y-diry[i],CELL_CHKNOREACH);i++);
		if (i==9) return 0; //No valid cells.
		dx = dirx[i];
		dy = diry[i];
	}

	if (x) *x = tbl->x-dx;
	if (y) *y = tbl->y-dy;
	return (path_search_real(&wpd,bl->m,bl->x,bl->y,tbl->x-dx,tbl->y-dy,easy,CELL_CHKNOREACH)!=-1);
}


/*==========================================
 * PCの攻撃 (timer関数)
 *------------------------------------------
 */
static int unit_attack_timer_sub(struct block_list* src, int tid, unsigned int tick)
{
	struct block_list *target;
	struct unit_data *ud;
	struct map_session_data *sd = NULL;
	struct mob_data *md = NULL;
	int range;
	
	if((ud=unit_bl2ud(src))==NULL)
		return 0;
	if(ud->attacktimer != tid){
		if(battle_config.error_log)
			ShowError("unit_attack_timer %d != %d\n",ud->attacktimer,tid);
		return 0;
	}
	BL_CAST( BL_PC , src, sd);
	BL_CAST( BL_MOB, src, md);
	ud->attacktimer=-1;
	target=map_id2bl(ud->target);

	if(src->prev == NULL || target==NULL || target->prev == NULL)
		return 0;

	if(ud->skilltimer != -1 && (!sd || pc_checkskill(sd,SA_FREECAST) <= 0))
		return 0;
	
	if(src->m != target->m || status_isdead(src) || status_isdead(target) || !status_check_skilluse(src, target, 0, 0))
		return 0;

	if(!battle_config.sdelay_attack_enable &&
		DIFF_TICK(ud->canact_tick,tick) > 0 && 
		(!sd || pc_checkskill(sd,SA_FREECAST) <= 0)
	) {
		if (tid == -1) { //requested attack.
			if(sd) clif_skill_fail(sd,1,4,0);
			return 0;
		}
		//Otherwise, we are in a combo-attack, delay this until your canact time is over. [Skotlex]
		if(ud->state.attack_continue) {
			if (DIFF_TICK(ud->canact_tick, ud->attackabletime) > 0)
				ud->attackabletime = ud->canact_tick;
			ud->attacktimer=add_timer(ud->attackabletime,unit_attack_timer,src->id,0);
		}
		return 1;
	}

	range = status_get_range(src);
	
	if(!sd || sd->status.weapon != 11) range++; //Dunno why everyone but bows gets this extra range...
	if(unit_is_walking(target)) range++; //Extra range when chasing

	if(!check_distance_bl(src,target,range) ) {
		//Chase if required.
		if(ud->state.attack_continue) {
			if(sd)
				clif_movetoattack(sd,target);
			else
				unit_walktobl(src,target,ud->chaserange,ud->state.walk_easy|2);
		}
		return 1;
	}
	if(!battle_check_range(src,target,range)) {
	  	//Within range, but no direct line of attack
		if(ud->state.attack_continue) {
			if(ud->chaserange > 2) ud->chaserange-=2;
			unit_walktobl(src,target,ud->chaserange,ud->state.walk_easy|2);
		}
		return 1;
	}

	if(DIFF_TICK(ud->attackabletime,tick) <= 0) {
		if (battle_config.attack_direction_change &&
			(src->type&battle_config.attack_direction_change)) {
			ud->dir = map_calc_dir(src, target->x,target->y );
			if (sd) sd->head_dir = ud->dir;
		}
		if(ud->walktimer != -1)
			unit_stop_walking(src,1);
		if(md) {
			if (mobskill_use(md,tick,-1))
				return 1;
			if (status_get_mode(src)&MD_ASSIST && DIFF_TICK(md->last_linktime, tick) < MIN_MOBLINKTIME)
			{	// Link monsters nearby [Skotlex]
				md->last_linktime = tick;
				map_foreachinrange(mob_linksearch, src, md->db->range2,
					BL_MOB, md->class_, target, tick);
			}
		}
		if(src->type == BL_PET && pet_attackskill((TBL_PET*)src, target->id))
			return 1;
		
		map_freeblock_lock();
		ud->attacktarget_lv = battle_weapon_attack(src,target,tick,0);

		if(sd && sd->status.pet_id > 0 && sd->pd && sd->petDB && battle_config.pet_attack_support)
			pet_target_check(sd,target,0);
		map_freeblock_unlock();

		if(ud->skilltimer != -1 && sd && (range = pc_checkskill(sd,SA_FREECAST)) > 0 ) // フリーキャスト
			ud->attackabletime = tick + (status_get_adelay(src)*(150 - range*5)/100);
		else
			ud->attackabletime = tick + status_get_adelay(src);

//		You can't move if you can't attack neither.
		unit_set_walkdelay(src, tick, status_get_amotion(src), 1);
	}

	if(ud->state.attack_continue)
		ud->attacktimer = add_timer(ud->attackabletime,unit_attack_timer,src->id,0);

	return 1;
}

static int unit_attack_timer(int tid,unsigned int tick,int id,int data) {
	struct block_list *bl;
	bl = map_id2bl(id);
	if(bl && unit_attack_timer_sub(bl, tid, tick) == 0)
		unit_unattackable(bl);
	return 0;
}

/*==========================================
 * Cancels an ongoing skill cast.
 * flag&1: Cast-Cancel invoked.
 * flag&2: Cancel only if skill is cancellable.
 *------------------------------------------
 */
int unit_skillcastcancel(struct block_list *bl,int type)
{
	struct map_session_data *sd = NULL;
	struct unit_data *ud = unit_bl2ud( bl);
	unsigned int tick=gettick();
	int ret=0, skill;
	
	nullpo_retr(0, bl);
	if (!ud || ud->skilltimer==-1)
		return 0; //Nothing to cancel.

	BL_CAST(BL_PC,  bl, sd);

	if (type&2) {
		//See if it can be cancelled.
		if (!ud->state.skillcastcancel)
			return 0;

		if (sd && (sd->special_state.no_castcancel2 ||
			(sd->special_state.no_castcancel && !map_flag_gvg(bl->m)))) //fixed flags being read the wrong way around [blackhole89]
			return 0;
	}
	
	ud->canact_tick=tick;
	if(sd && (skill = pc_checkskill(sd,SA_FREECAST)) > 0)
		status_quick_recalc_speed(sd, SA_FREECAST, skill, 0);	//Updated to use calc_speed [Skotlex]
	
	if(type&1 && sd)
		skill = sd->skillid_old;
	else
		skill = ud->skillid;
	
	if (skill_get_inf(skill) & INF_GROUND_SKILL)
		ret=delete_timer( ud->skilltimer, skill_castend_pos );
	else
		ret=delete_timer( ud->skilltimer, skill_castend_id );
	if(ret<0)
		ShowError("delete timer error : skillid : %d\n",ret);
	
	if(bl->type==BL_MOB) ((TBL_MOB*)bl)->skillidx  = -1;

	ud->skilltimer = -1;
	clif_skillcastcancel(bl);
	return 1;
}

// unit_data の初期化処理
void unit_dataset(struct block_list *bl) {
	struct unit_data *ud;
	nullpo_retv(ud = unit_bl2ud(bl));

	memset( ud, 0, sizeof( struct unit_data) );
	ud->bl             = bl;
	ud->walktimer      = -1;
	ud->skilltimer     = -1;
	ud->attacktimer    = -1;
	ud->attackabletime = 
	ud->canact_tick    = 
	ud->canmove_tick   = gettick();
}

/*==========================================
 * 自分をロックしているユニットの数を数える(foreachclient)
 *------------------------------------------
 */
static int unit_counttargeted_sub(struct block_list *bl, va_list ap)
{
	int id, target_lv;
	struct unit_data *ud;
	id = va_arg(ap,int);
	target_lv = va_arg(ap,int);
	if(bl->id == id)
		return 0;

	ud = unit_bl2ud(bl);

	if (ud && ud->target == id && ud->attacktimer != -1 && ud->attacktarget_lv >= target_lv)
		return 1;

	return 0;	
}

/*==========================================
 *
 *------------------------------------------
 */
int unit_fixdamage(struct block_list *src,struct block_list *target,unsigned int tick,int sdelay,int ddelay,int damage,int div,int type,int damage2)
{
	nullpo_retr(0, target);

	if(damage+damage2 <= 0)
		return 0;
	
	return battle_damage(src,target,damage+damage2,clif_damage(target,target,tick,sdelay,ddelay,damage,div,type,damage2),0);
}
/*==========================================
 * 自分をロックしている対象の数を返す
 * 戻りは整数で0以上
 *------------------------------------------
 */
int unit_counttargeted(struct block_list *bl,int target_lv)
{
	nullpo_retr(0, bl);
	return (map_foreachinrange(unit_counttargeted_sub, bl, AREA_SIZE, BL_CHAR,
		bl->id, target_lv));
}

/*==========================================
 * idを攻撃しているPCの攻撃を停止
 * clif_foreachclientのcallback関数
 *------------------------------------------
 */
int unit_mobstopattacked(struct map_session_data *sd,va_list ap)
{
	int id=va_arg(ap,int);
	if(sd->ud.target==id)
		unit_stop_attack(&sd->bl);
	return 0;
}
/*==========================================
 * 見た目のサイズを変更する
 *------------------------------------------
 */
int unit_changeviewsize(struct block_list *bl,short size)
{
	nullpo_retr(0, bl);

	size=(size<0)?-1:(size>0)?1:0;

	if(bl->type == BL_PC) {
		((TBL_PC*)bl)->state.size=size;
	} else if(bl->type == BL_MOB) {
		((TBL_MOB*)bl)->special_state.size=size;
	} else
		return 0;
	if(size!=0)
		clif_misceffect2(bl,421+size);
	return 0;
}

/*==========================================
 * Removes a bl/ud from the map.
 * Returns 1 on success. 0 if it couldn't be removed or the bl was free'd
 * if clrtype is 1 (death), appropiate cleanup is performed. 
 * Otherwise it is assumed bl is being warped.
 *------------------------------------------
 */
int unit_remove_map(struct block_list *bl, int clrtype) {
	struct unit_data *ud = unit_bl2ud(bl);
	struct status_change *sc = status_get_sc(bl);
	nullpo_retr(0, ud);

	if(bl->prev == NULL)
		return 0; //Already removed?

	map_freeblock_lock();

	ud->target = 0; //Unlock walk/attack target.
	if (ud->walktimer != -1)
		unit_stop_walking(bl,0);
	if (ud->attacktimer != -1)
		unit_stop_attack(bl);
	if (ud->skilltimer != -1)
		unit_skillcastcancel(bl,0);
	ud->attackabletime = ud->canmove_tick = ud->canact_tick = gettick();
	clif_clearchar_area(bl,clrtype);
	
	if (clrtype == 1) //Death. Remove all status changes.
		status_change_clear(bl,0);
	else if(sc && sc->count ) { //map-change/warp dispells.
		if(sc->data[SC_BLADESTOP].timer!=-1)
			status_change_end(bl,SC_BLADESTOP,-1);
		if(sc->data[SC_BASILICA].timer!=-1)
			status_change_end(bl,SC_BASILICA,-1);
		if(sc->data[SC_ANKLE].timer != -1)
			status_change_end(bl, SC_ANKLE, -1);
		if (sc->data[SC_TRICKDEAD].timer != -1)
			status_change_end(bl, SC_TRICKDEAD, -1);
		if (sc->data[SC_BLADESTOP].timer!=-1)
			status_change_end(bl,SC_BLADESTOP,-1);
		if (sc->data[SC_RUN].timer!=-1)
			status_change_end(bl,SC_RUN,-1);
		if (sc->data[SC_DANCING].timer!=-1) // clear dance effect when warping [Valaris]
			skill_stop_dancing(bl);
		if (sc->data[SC_DEVOTION].timer!=-1)
			status_change_end(bl,SC_DEVOTION,-1);
		if (sc->data[SC_CLOSECONFINE].timer!=-1)
			status_change_end(bl,SC_CLOSECONFINE,-1);
		if (sc->data[SC_CLOSECONFINE2].timer!=-1)
			status_change_end(bl,SC_CLOSECONFINE2,-1);
		if (sc->data[SC_HIDING].timer!=-1)
			status_change_end(bl, SC_HIDING, -1);
		if (sc->data[SC_CLOAKING].timer!=-1)
			status_change_end(bl, SC_CLOAKING, -1);
		if (sc->data[SC_CHASEWALK].timer!=-1)
			status_change_end(bl, SC_CHASEWALK, -1);
		if (sc->data[SC_GOSPEL].timer != -1 && sc->data[SC_GOSPEL].val4 == BCT_SELF)
			status_change_end(bl, SC_GOSPEL, -1);
	}

	if (clrtype == 1 && battle_config.clear_unit_ondeath && //Clrtype 1 = died.
		battle_config.clear_unit_ondeath&bl->type)
		skill_clear_unitgroup(bl);			// スキルユニットグループの削除
	if (bl->type&BL_CHAR) {
		skill_unit_move(bl,gettick(),4);
		skill_cleartimerskill(bl);			// タイマースキルクリア
	}

	if(bl->type == BL_PC) {
		struct map_session_data *sd = (struct map_session_data*)bl;

		//Leave/reject all invitations.
		if(sd->chatID)
			chat_leavechat(sd);
		if(sd->trade_partner)
			trade_tradecancel(sd);
		if(sd->vender_id)
			vending_closevending(sd);
		if(sd->state.storage_flag == 1)
			storage_storage_quit(sd,0);
		else if (sd->state.storage_flag == 2)
			storage_guild_storage_quit(sd,0);

		if(sd->party_invite>0)
			party_reply_invite(sd,sd->party_invite_account,0);
		if(sd->guild_invite>0)
			guild_reply_invite(sd,sd->guild_invite,0);
		if(sd->guild_alliance>0)
			guild_reply_reqalliance(sd,sd->guild_alliance_account,0);

		pc_stop_following(sd);
		pc_delinvincibletimer(sd);

		if(sd->pvp_timer!=-1) {
			delete_timer(sd->pvp_timer,pc_calc_pvprank_timer);
			sd->pvp_timer = -1;
		}

		if(pc_issit(sd)) {
			pc_setstand(sd);
			skill_gangsterparadise(sd,0);
			skill_rest(sd,0);
		}
		party_send_dot_remove(sd);//minimap dot fix [Kevin]
		guild_send_dot_remove(sd);
	} else if(bl->type == BL_MOB) {
		struct mob_data *md = (struct mob_data*)bl;
		md->target_id=0;
		md->attacked_id=0;
		md->state.skillstate= clrtype==1?MSS_DEAD:MSS_IDLE;
		if (md->master_id) md->master_dist = 0;
		if (clrtype == 1) { //Death.
			md->last_deadtime=gettick();
//			Isn't this too much? Why not let the attack-timer fail when the mob is dead? [Skotlex]
//			clif_foreachclient(unit_mobstopattacked,md->bl.id);
			if(md->deletetimer!=-1)
				delete_timer(md->deletetimer,mob_timer_delete);
			md->deletetimer=-1;
			md->hp=0;
			if(pcdb_checkid(md->vd->class_)) //Player mobs are not removed automatically by the client.
				clif_clearchar_delay(gettick()+3000,bl,0);
			mob_deleteslave(md);

			if(!md->spawn) {
				map_delblock(bl);
				unit_free(bl); //Mob does not respawn.
				map_freeblock_unlock();
				return 0;
			}
			mob_setdelayspawn(md); //Set respawning.
		}
	} else if (bl->type == BL_PET) {
		struct pet_data *pd = (struct pet_data*)bl;
		struct map_session_data *sd = pd->msd;
		
		if(!sd) {
			map_delblock(bl);
			unit_free(bl);
			map_freeblock_unlock();
			return 0;
		}
		if (sd->pet.intimate <= 0)
		{	//Remove pet.
			intif_delete_petdata(sd->status.pet_id);
			sd->status.pet_id = 0;
			sd->pd = NULL;
			sd->petDB = NULL;
			pd->msd = NULL;
			if(battle_config.pet_status_support)
				status_calc_pc(sd,2);
			map_delblock(bl);
			unit_free(bl);
			map_freeblock_unlock();
			return 0;
		}
	}
	map_delblock(bl);
	map_freeblock_unlock();
	return 1;
}

/*==========================================
 * Function to free all related resources to the bl
 * if unit is on map, it is removed using clrtype 0.
 *------------------------------------------
 */

int unit_free(struct block_list *bl) {
	struct unit_data *ud = unit_bl2ud( bl );
	nullpo_retr(0, ud);

	map_freeblock_lock();
	if( bl->prev )	//Players are supposed to logout with a "warp" effect.
		unit_remove_map(bl, bl->type==BL_PC?3:0);
	
	if( bl->type == BL_PC ) {
		struct map_session_data *sd = (struct map_session_data*)bl;
		if(status_isdead(bl))
			pc_setrestartvalue(sd,2);

		//Status that are not saved...
		if(sd->sc.count) {
			if(sd->sc.data[SC_SPURT].timer!=-1)
				status_change_end(bl,SC_SPURT,-1);
			if(sd->sc.data[SC_BERSERK].timer!=-1)
				status_change_end(bl,SC_BERSERK,-1);
			if(sd->sc.data[SC_TRICKDEAD].timer!=-1)
				status_change_end(bl,SC_TRICKDEAD,-1);
			if (battle_config.debuff_on_logout) {
				if(sd->sc.data[SC_STRIPWEAPON].timer!=-1)
					status_change_end(bl,SC_STRIPWEAPON,-1);
				if(sd->sc.data[SC_STRIPARMOR].timer!=-1)
					status_change_end(bl,SC_STRIPARMOR,-1);
				if(sd->sc.data[SC_STRIPSHIELD].timer!=-1)
					status_change_end(bl,SC_STRIPSHIELD,-1);
				if(sd->sc.data[SC_STRIPHELM].timer!=-1)
					status_change_end(bl,SC_STRIPHELM,-1);
				if(sd->sc.data[SC_EXTREMITYFIST].timer!=-1)
					status_change_end(bl,SC_EXTREMITYFIST,-1);
				if(sd->sc.data[SC_EXPLOSIONSPIRITS].timer!=-1)
					status_change_end(bl,SC_EXPLOSIONSPIRITS,-1);
			}
		}
		// Notify friends that this char logged out. [Skotlex]
		clif_foreachclient(clif_friendslist_toggle_sub, sd->status.account_id, sd->status.char_id, 0);
		party_send_logout(sd);
		guild_send_memberinfoshort(sd,0);
		pc_cleareventtimer(sd);		
		pc_delspiritball(sd,sd->spiritball,1);
		chrif_save_scdata(sd); //Save status changes, then clear'em out from memory. [Skotlex]
		storage_delete(sd->status.account_id);
		pc_makesavestatus(sd);
		sd->state.waitingdisconnect = 1;
	} else if( bl->type == BL_PET ) {
		struct pet_data *pd = (struct pet_data*)bl;
		struct map_session_data *sd = pd->msd;
		if(sd && sd->pet_hungry_timer != -1)
			pet_hungry_timer_delete(sd);
		if (pd->a_skill)
		{
			aFree(pd->a_skill);
			pd->a_skill = NULL;
		}
		if (pd->s_skill)
		{
			if (pd->s_skill->timer != -1) {
				if (pd->s_skill->id)
					delete_timer(pd->s_skill->timer, pet_skill_support_timer);
				else
					delete_timer(pd->s_skill->timer, pet_heal_timer);
			}
			aFree(pd->s_skill);
			pd->s_skill = NULL;
		}
		if(pd->recovery)
		{
			if(pd->recovery->timer != -1)
				delete_timer(pd->recovery->timer, pet_recovery_timer);
			aFree(pd->recovery);
			pd->recovery = NULL;
		}
		if(pd->bonus)
		{
			if (pd->bonus->timer != -1)
				delete_timer(pd->bonus->timer, pet_skill_bonus_timer);
			aFree(pd->bonus);
			pd->bonus = NULL;
		}
		if (pd->loot)
		{
			if (pd->loot->item)
				aFree(pd->loot->item);
			aFree (pd->loot);
			pd->loot = NULL;
		}
		if (pd->status)
		{
			aFree(pd->status);
			pd->status = NULL;
		}
		if (sd) sd->pd = NULL;
	} else if(bl->type == BL_MOB) {
		struct mob_data *md = (struct mob_data*)bl;
		if(md->deletetimer!=-1)
			delete_timer(md->deletetimer,mob_timer_delete);
		md->deletetimer=-1;
		if(md->lootitem) {
			aFree(md->lootitem);
			md->lootitem=NULL;
		}
		if (md->guardian_data)
		{
			if (md->guardian_data->number < MAX_GUARDIANS)
				md->guardian_data->castle->guardian[md->guardian_data->number].id = 0;
			aFree(md->guardian_data);
			md->guardian_data = NULL;
		}
		if (md->spawn && md->spawn_n < 0 && --(md->spawn->num) == 0)
		{	//Spawning data is not attached to the map, so free it
			//if this is the last mob who is pointing at it.
			aFree(md->spawn);
			md->spawn = NULL;
		}
		if(mob_is_clone(md->class_))
			mob_clone_delete(md->class_);
	}

	skill_clear_unitgroup(bl);
	status_change_clear(bl,1);
	if (bl->type != BL_PC)
  	{	//Players are handled by map_quit
		map_deliddb(bl);
		map_freeblock(bl);
	}
	map_freeblock_unlock();
	return 0;
}

int do_init_unit(void) {
	add_timer_func_list(unit_attack_timer,  "unit_attack_timer");
	add_timer_func_list(unit_walktoxy_timer,"unit_walktoxy_timer");
	add_timer_func_list(unit_walktobl_sub, "unit_walktobl_sub");

	return 0;
}

int do_final_unit(void) {
	// nothing to do
	return 0;
}

