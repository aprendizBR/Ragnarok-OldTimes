#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <arpa/inet.h>

#include "socket.h"
#include "timer.h"
#include "map.h"
#include "chrif.h"
#include "clif.h"
#include "pc.h"
#include "intif.h"
#include "storage.h"
#include "party.h"
#include "guild.h"
#include "pet.h"

#ifdef MEMWATCH
#include "memwatch.h"
#endif

static const int packet_len_table[]={
	-1,-1,27, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0,  0, 0,
	-1, 7, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0,  0, 0,
	35,-1,11,15, 34,29, 7,-1,  0, 0, 0, 0,  0, 0,  0, 0,
	10,-1,15, 0, 79,17, 7,-1,  0,-1,-1,-1, 14,67,186,-1,
	 0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0,  0, 0,
	 0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0,  0, 0,
	 0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0,  0, 0,
	 0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0,  0, 0,
	11,-1, 7, 3,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0,  0, 0,
};

extern int char_fd;		// inter serverのfdはchar_fdを使う
#define inter_fd (char_fd)	// エイリアス

//-----------------------------------------------------------------
// inter serverへの送信


// pet
int intif_create_pet(int account_id,int char_id,short pet_class,short pet_lv,short pet_egg_id,
	short pet_equip,short intimate,short hungry,char rename_flag,char incuvate,char *pet_name)
{
	WFIFOW(inter_fd,0) = 0x3080;
	WFIFOL(inter_fd,2) = account_id;
	WFIFOL(inter_fd,6) = char_id;
	WFIFOW(inter_fd,10) = pet_class;
	WFIFOW(inter_fd,12) = pet_lv;
	WFIFOW(inter_fd,14) = pet_egg_id;
	WFIFOW(inter_fd,16) = pet_equip;
	WFIFOW(inter_fd,18) = intimate;
	WFIFOW(inter_fd,20) = hungry;
	WFIFOB(inter_fd,22) = rename_flag;
	WFIFOB(inter_fd,23) = incuvate;
	memcpy(WFIFOP(inter_fd,24),pet_name,24);
	WFIFOSET(inter_fd,48);

	return 0;
}

int intif_request_petdata(int account_id,int char_id,int pet_id)
{
	WFIFOW(inter_fd,0) = 0x3081;
	WFIFOL(inter_fd,2) = account_id;
	WFIFOL(inter_fd,6) = char_id;
	WFIFOL(inter_fd,10) = pet_id;
	WFIFOSET(inter_fd,14);

	return 0;
}

int intif_save_petdata(int account_id,struct s_pet *p)
{
	WFIFOW(inter_fd,0) = 0x3082;
	WFIFOW(inter_fd,2) = sizeof(struct s_pet) + 8;
	WFIFOL(inter_fd,4) = account_id;
	memcpy(WFIFOP(inter_fd,8),p,sizeof(struct s_pet));
	WFIFOSET(inter_fd,WFIFOW(inter_fd,2));

	return 0;
}

int intif_delete_petdata(int pet_id)
{
	WFIFOW(inter_fd,0) = 0x3083;
	WFIFOL(inter_fd,2) = pet_id;
	WFIFOSET(inter_fd,6);

	return 0;
}

// GMメッセージを送信
int intif_GMmessage(char* mes,int len,int flag)
{
	
	int lp=(flag&0x10)?8:4;
	WFIFOW(inter_fd,0) = 0x3000;
	WFIFOW(inter_fd,2) = lp+len;
	WFIFOL(inter_fd,4) = 0x65756c62;
	memcpy(WFIFOP(inter_fd,lp), mes, len);
	WFIFOSET(inter_fd, WFIFOW(inter_fd,2) );

	return 0;
}

// Wisの送信
int intif_wis_message(struct map_session_data *sd,char *nick,char *mes,int mes_len)
{
	WFIFOW(inter_fd,0) = 0x3001;
	WFIFOW(inter_fd,2) = mes_len+52;
	memcpy(WFIFOP(inter_fd,4),sd->status.name,24);
	memcpy(WFIFOP(inter_fd,28),nick,24);
	memcpy(WFIFOP(inter_fd,52),mes,mes_len);
	WFIFOSET(inter_fd, WFIFOW(inter_fd,2) );
//	printf("intif_wis_message %s %s %s\n",sd->status.name,nick,mes);
	return 0;
}

// Wisの返事
int intif_wis_replay(int id,int flag)
{
	WFIFOW(inter_fd,0) = 0x3002;
	WFIFOW(inter_fd,2) = id;
	WFIFOB(inter_fd,4) = flag;
	WFIFOSET(inter_fd,5);
//	printf("intif_wis_replay %d %d\n",id,flag);
	return 0;
}

// 倉庫データ要求
int intif_request_storage(int account_id)
{
	WFIFOW(inter_fd,0) = 0x3010;
	WFIFOL(inter_fd,2) = account_id;
	WFIFOSET(inter_fd,6);
	return 0;
}
// 倉庫データ送信
int intif_send_storage(int account_id)
{
	WFIFOW(inter_fd,0) = 0x3011;
	WFIFOW(inter_fd,2) = sizeof(struct storage)+8;
	WFIFOL(inter_fd,4) = account_id;
	memcpy( WFIFOP(inter_fd,8),
		 &storage[ account2storage(account_id) ], sizeof(struct storage) );
	WFIFOSET(inter_fd,WFIFOW(inter_fd,2));
	return 0;
}

// パーティ作成要求
int intif_create_party(struct map_session_data *sd,char *name)
{
	WFIFOW(inter_fd,0) = 0x3020;
	WFIFOL(inter_fd,2) = sd->status.account_id;
	memcpy(WFIFOP(inter_fd, 6),name,24);
	memcpy(WFIFOP(inter_fd,30),sd->status.name,24);
	memcpy(WFIFOP(inter_fd,54),map[sd->bl.m].name,16);
	WFIFOW(inter_fd,70)= sd->status.base_level;
	WFIFOSET(inter_fd,72);
//	printf("intif: create party\n");
	return 0;
}
// パーティ情報要求
int intif_request_partyinfo(int party_id)
{
	WFIFOW(inter_fd,0) = 0x3021;
	WFIFOL(inter_fd,2) = party_id;
	WFIFOSET(inter_fd,6);
//	printf("intif: request party info\n");
	return 0;
}
// パーティ追加要求
int intif_party_addmember(int party_id,int account_id)
{
	struct map_session_data *sd;
	sd=map_id2sd(account_id);
//	printf("intif: party add member %d %d\n",party_id,account_id);
	if(sd!=NULL){
		WFIFOW(inter_fd,0)=0x3022;
		WFIFOL(inter_fd,2)=party_id;
		WFIFOL(inter_fd,6)=account_id;
		memcpy(WFIFOP(inter_fd,10),sd->status.name,24);
		memcpy(WFIFOP(inter_fd,34),map[sd->bl.m].name,16);
		WFIFOW(inter_fd,50)=sd->status.base_level;
		WFIFOSET(inter_fd,52);
	}
	return 0;
}
// パーティ設定変更
int intif_party_changeoption(int party_id,int account_id,int exp,int item)
{
	WFIFOW(inter_fd,0)=0x3023;
	WFIFOL(inter_fd,2)=party_id;
	WFIFOL(inter_fd,6)=account_id;
	WFIFOW(inter_fd,10)=exp;
	WFIFOW(inter_fd,12)=item;
	WFIFOSET(inter_fd,14);
	return 0;
}
// パーティ脱退要求
int intif_party_leave(int party_id,int account_id)
{
//	printf("intif: party leave %d %d\n",party_id,account_id);
	WFIFOW(inter_fd,0)=0x3024;
	WFIFOL(inter_fd,2)=party_id;
	WFIFOL(inter_fd,6)=account_id;
	WFIFOSET(inter_fd,10);
	return 0;
}
// パーティ移動要求
int intif_party_changemap(struct map_session_data *sd,int online)
{
	if(sd!=NULL){
		WFIFOW(inter_fd,0)=0x3025;
		WFIFOL(inter_fd,2)=sd->status.party_id;
		WFIFOL(inter_fd,6)=sd->status.account_id;
		memcpy(WFIFOP(inter_fd,10),map[sd->bl.m].name,16);
		WFIFOB(inter_fd,26)=online;
		WFIFOW(inter_fd,27)=sd->status.base_level;
		WFIFOSET(inter_fd,29);
	}
//	printf("party: change map\n");
	return 0;
}
// パーティー解散要求
int intif_break_party(int party_id)
{
	WFIFOW(inter_fd,0)=0x3026;
	WFIFOL(inter_fd,2)=party_id;
	WFIFOSET(inter_fd,6);
	return 0;
}
// パーティ会話送信
int intif_party_message(int party_id,int account_id,char *mes,int len)
{
//	printf("intif_party_message: %s\n",mes);
	WFIFOW(inter_fd,0)=0x3027;
	WFIFOW(inter_fd,2)=len+12;
	WFIFOL(inter_fd,4)=party_id;
	WFIFOL(inter_fd,8)=account_id;
	memcpy(WFIFOP(inter_fd,12),mes,len);
	WFIFOSET(inter_fd,len+12);
	return 0;
}
// パーティ競合チェック要求
int intif_party_checkconflict(int party_id,int account_id,char *nick)
{
	WFIFOW(inter_fd,0)=0x3028;
	WFIFOL(inter_fd,2)=party_id;
	WFIFOL(inter_fd,6)=account_id;
	memcpy(WFIFOP(inter_fd,10),nick,24);
	WFIFOSET(inter_fd,34);
	return 0;
}

// ギルド作成要求
int intif_guild_create(const char *name,const struct guild_member *master)
{
	WFIFOW(inter_fd,0)=0x3030;
	WFIFOW(inter_fd,2)=sizeof(struct guild_member)+32;
	WFIFOL(inter_fd,4)=master->account_id;
	memcpy(WFIFOP(inter_fd,8),name,24);
	memcpy(WFIFOP(inter_fd,32),master,sizeof(struct guild_member));
	WFIFOSET(inter_fd,WFIFOW(inter_fd,2));
	return 0;
}
// ギルド情報要求
int intif_guild_request_info(int guild_id)
{
	WFIFOW(inter_fd,0) = 0x3031;
	WFIFOL(inter_fd,2) = guild_id;
	WFIFOSET(inter_fd,6);
	return 0;
}
// ギルドメンバ追加要求
int intif_guild_addmember(int guild_id,struct guild_member *m)
{
	WFIFOW(inter_fd,0) = 0x3032;
	WFIFOW(inter_fd,2) = sizeof(struct guild_member)+8;
	WFIFOL(inter_fd,4) = guild_id;
	memcpy(WFIFOP(inter_fd,8),m,sizeof(struct guild_member));
	WFIFOSET(inter_fd,WFIFOW(inter_fd,2));
	return 0;
}
// ギルドメンバ脱退/追放要求
int intif_guild_leave(int guild_id,int account_id,int char_id,int flag,const char *mes)
{
	WFIFOW(inter_fd, 0) = 0x3034;
	WFIFOL(inter_fd, 2) = guild_id;
	WFIFOL(inter_fd, 6) = account_id;
	WFIFOL(inter_fd,10) = char_id;
	WFIFOB(inter_fd,14) = flag;
	memcpy(WFIFOP(inter_fd,15),mes,40);
	WFIFOSET(inter_fd,55);
	return 0;
}
// ギルドメンバのオンライン状況/Lv更新要求
int intif_guild_memberinfoshort(int guild_id,
	int account_id,int char_id,int online,int lv)
{
	WFIFOW(inter_fd, 0) = 0x3035;
	WFIFOL(inter_fd, 2) = guild_id;
	WFIFOL(inter_fd, 6) = account_id;
	WFIFOL(inter_fd,10) = char_id;
	WFIFOB(inter_fd,14) = online;
	WFIFOW(inter_fd,15) = lv;
	WFIFOSET(inter_fd,17);
	return 0;
}
// ギルド解散通知
int intif_guild_break(int guild_id)
{
	WFIFOW(inter_fd, 0) = 0x3036;
	WFIFOL(inter_fd, 2) = guild_id;
	WFIFOSET(inter_fd,6);
	return 0;
}
// ギルド会話送信
int intif_guild_message(int guild_id,int account_id,char *mes,int len)
{
	WFIFOW(inter_fd,0)=0x3037;
	WFIFOW(inter_fd,2)=len+12;
	WFIFOL(inter_fd,4)=guild_id;
	WFIFOL(inter_fd,8)=account_id;
	memcpy(WFIFOP(inter_fd,12),mes,len);
	WFIFOSET(inter_fd,len+12);
	return 0;
}
// ギルド競合チェック要求
int intif_guild_checkconflict(int guild_id,int account_id,int char_id)
{
	WFIFOW(inter_fd, 0)=0x3038;
	WFIFOL(inter_fd, 2)=guild_id;
	WFIFOL(inter_fd, 6)=account_id;
	WFIFOL(inter_fd,10)=char_id;
	WFIFOSET(inter_fd,14);
	return 0;
}
// ギルド基本情報変更要求
int intif_guild_change_basicinfo(int guild_id,int type,const void *data,int len)
{
	WFIFOW(inter_fd,0)=0x3039;
	WFIFOW(inter_fd,2)=len+10;
	WFIFOL(inter_fd,4)=guild_id;
	WFIFOW(inter_fd,8)=type;
	memcpy(WFIFOP(inter_fd,10),data,len);
	WFIFOSET(inter_fd,len+10);
	return 0;
}
// ギルドメンバ情報変更要求
int intif_guild_change_memberinfo(int guild_id,int account_id,int char_id,
	int type,const void *data,int len)
{
	WFIFOW(inter_fd, 0)=0x303a;
	WFIFOW(inter_fd, 2)=len+18;
	WFIFOL(inter_fd, 4)=guild_id;
	WFIFOL(inter_fd, 8)=account_id;
	WFIFOL(inter_fd,12)=char_id;
	WFIFOW(inter_fd,16)=type;
	memcpy(WFIFOP(inter_fd,18),data,len);
	WFIFOSET(inter_fd,len+18);
	return 0;
}
// ギルド役職変更要求
int intif_guild_position(int guild_id,int idx,struct guild_position *p)
{
	WFIFOW(inter_fd,0)=0x303b;
	WFIFOW(inter_fd,2)=sizeof(struct guild_position)+12;
	WFIFOL(inter_fd,4)=guild_id;
	WFIFOL(inter_fd,8)=idx;
	memcpy(WFIFOP(inter_fd,12),p,sizeof(struct guild_position));
	WFIFOSET(inter_fd,WFIFOW(inter_fd,2));
	return 0;
}
// ギルドスキルアップ要求
int intif_guild_skillup(int guild_id,int skill_num,int account_id)
{
	WFIFOW(inter_fd, 0)=0x303c;
	WFIFOL(inter_fd, 2)=guild_id;
	WFIFOL(inter_fd, 6)=skill_num;
	WFIFOL(inter_fd,10)=account_id;
	WFIFOSET(inter_fd,14);
	return 0;
}
// ギルド同盟/敵対要求
int intif_guild_alliance(int guild_id1,int guild_id2,int account_id1,int account_id2,int flag)
{
	WFIFOW(inter_fd, 0)=0x303d;
	WFIFOL(inter_fd, 2)=guild_id1;
	WFIFOL(inter_fd, 6)=guild_id2;
	WFIFOL(inter_fd,10)=account_id1;
	WFIFOL(inter_fd,14)=account_id2;
	WFIFOB(inter_fd,18)=flag;
	WFIFOSET(inter_fd,19);
	return 0;
}
// ギルド告知変更要求
int intif_guild_notice(int guild_id,const char *mes1,const char *mes2)
{
	WFIFOW(inter_fd,0)=0x303e;
	WFIFOL(inter_fd,2)=guild_id;
	memcpy(WFIFOP(inter_fd,6),mes1,60);
	memcpy(WFIFOP(inter_fd,66),mes2,120);
	WFIFOSET(inter_fd,186);
	return 0;
}
// ギルドエンブレム変更要求
int intif_guild_emblem(int guild_id,int len,const char *data)
{
	if(guild_id<=0 || len<0 || len>2000)
		return 0;
	WFIFOW(inter_fd,0)=0x303f;
	WFIFOW(inter_fd,2)=len+12;
	WFIFOL(inter_fd,4)=guild_id;
	WFIFOL(inter_fd,8)=0;
	memcpy(WFIFOP(inter_fd,12),data,len);
	WFIFOSET(inter_fd,len+12);
	return 0;
}


//-----------------------------------------------------------------
// inter serverから受信

// wis受信
int intif_parse_WisMessage(int fd)
{
	struct map_session_data* sd;
	int id=RFIFOW(fd,4);

//	printf("intif_parse_wismessage: %d %s %s %s\n",id,RFIFOP(fd,6),RFIFOP(fd,30),RFIFOP(fd,54) );
	
	sd=map_nick2sd(RFIFOP(fd,30));	// 送信先を探す
	if(sd!=NULL){
		clif_wis_message(sd->fd,RFIFOP(fd,6),RFIFOP(fd,54),RFIFOW(fd,2)-54);
		
		intif_wis_replay(id,0);	// 送信成功
		
	}else{
		intif_wis_replay(id,1);	// そんな人いません
	}
	return 0;
}

// wis送信結果受信
int intif_parse_WisEnd(int fd)
{
	struct map_session_data* sd;
//	printf("intif_parse_wisend: %s %d\n",RFIFOP(fd,2),RFIFOB(fd,26));
	sd=map_nick2sd(RFIFOP(fd,2));
	if(sd!=NULL)
		clif_wis_end(sd->fd,RFIFOB(fd,26));
	return 0;
}

// 倉庫データ受信
int intif_parse_LoadStorage(int fd)
{
	struct storage *stor;
	struct map_session_data *sd;
	stor=&storage[account2storage( RFIFOL(fd,4) )];
	if( RFIFOW(fd,2)-8 != sizeof(struct storage) ){
		printf("intif_parse_LoadStorage: data size error %d %d\n",RFIFOW(fd,2)-8 , sizeof(struct storage));
		return 1;
	}
	sd=map_id2sd( RFIFOL(fd,4) );
	if(sd==NULL){
		printf("intif_parse_LoadStorage: user not found %d\n",RFIFOL(fd,4));
		return 1;
	}
	printf("intif_openstorage: %d\n",RFIFOL(fd,4) );
	memcpy(stor,RFIFOP(fd,8),sizeof(struct storage));
	stor->storage_status=1;
	clif_storageitemlist(sd,stor);
	clif_storageequiplist(sd,stor);
	clif_updatestorageamount(sd,stor);
	return 0;
}
// 倉庫データ送信成功
int intif_parse_SaveStorage(int fd)
{
	printf("intif_savestorage: done %d %d\n",RFIFOL(fd,2),RFIFOB(fd,6) );
	return 0;
}

// パーティ作成可否
int intif_parse_PartyCreated(int fd)
{
	printf("intif: party created\n");
	party_created(RFIFOL(fd,2),RFIFOB(fd,6),RFIFOL(fd,7),RFIFOP(fd,11));
	return 0;
}
// パーティ情報
int intif_parse_PartyInfo(int fd)
{
	if( RFIFOW(fd,2)==8){
		printf("intif: party noinfo %d\n",RFIFOL(fd,4));
		party_recv_noinfo(RFIFOL(fd,4));
		return 0;
	}

//	printf("intif: party info %d\n",RFIFOL(fd,4));
	if( RFIFOW(fd,2)!=sizeof(struct party)+4 ){
		printf("intif: party info : data size error %d %d %d\n",RFIFOL(fd,4),
			RFIFOW(fd,2),sizeof(struct party)+4);
	}
	party_recv_info((struct party *)RFIFOP(fd,4));
	return 0;
}
// パーティ追加通知
int intif_parse_PartyMemberAdded(int fd)
{
	printf("intif: party member added %d %d %d\n",RFIFOL(fd,2),RFIFOL(fd,6),RFIFOB(fd,10));
	party_member_added(RFIFOL(fd,2),RFIFOL(fd,6),RFIFOB(fd,10));
	return 0;
}
// パーティ設定変更通知
int intif_parse_PartyOptionChanged(int fd)
{
	party_optionchanged(RFIFOL(fd,2),RFIFOL(fd,6),RFIFOW(fd,10),RFIFOW(fd,12),RFIFOB(fd,14));
	return 0;
}
// パーティ脱退通知
int intif_parse_PartyMemberLeaved(int fd)
{
	printf("intif: party member leaved %d %d %s\n",RFIFOL(fd,2),RFIFOL(fd,6),RFIFOP(fd,10));
	party_member_leaved(RFIFOL(fd,2),RFIFOL(fd,6),RFIFOP(fd,10));
	return 0;
}
// パーティ解散通知
int intif_parse_PartyBroken(int fd)
{
	party_broken(RFIFOL(fd,2));
	return 0;
}
// パーティ移動通知
int intif_parse_PartyMove(int fd)
{
//	printf("intif: party move %d %d %s %d %d\n",
//		RFIFOL(fd,2),RFIFOL(fd,6),RFIFOP(fd,10),RFIFOB(fd,26),RFIFOW(fd,27));
	party_recv_movemap(RFIFOL(fd,2),RFIFOL(fd,6),RFIFOP(fd,10),RFIFOB(fd,26),RFIFOW(fd,27));
	return 0;
}
// パーティメッセージ
int intif_parse_PartyMessage(int fd)
{
//	printf("intif_parse_PartyMessage: %s\n",RFIFOP(fd,12));
	party_recv_message(RFIFOL(fd,4),RFIFOL(fd,8),RFIFOP(fd,12),RFIFOW(fd,2)-12);
	return 0;
}

// ギルド作成可否
int intif_parse_GuildCreated(int fd)
{
	guild_created(RFIFOL(fd,2),RFIFOL(fd,6));
	return 0;
}
// ギルド情報
int intif_parse_GuildInfo(int fd)
{
	if( RFIFOW(fd,2)==8){
		printf("intif: guild noinfo %d\n",RFIFOL(fd,4));
		guild_recv_noinfo(RFIFOL(fd,4));
		return 0;
	}

//	printf("intif: guild info %d\n",RFIFOL(fd,4));
	if( RFIFOW(fd,2)!=sizeof(struct guild)+4 ){
		printf("intif: guild info : data size error\n %d %d %d",RFIFOL(fd,4),
			RFIFOW(fd,2),sizeof(struct guild)+4);
	}
	guild_recv_info((struct guild *)RFIFOP(fd,4));
	return 0;
}
// ギルドメンバ追加通知
int intif_parse_GuildMemberAdded(int fd)
{
	printf("intif: guild member added %d %d %d %d\n",RFIFOL(fd,2),RFIFOL(fd,6),RFIFOL(fd,10),RFIFOB(fd,14));
	guild_member_added(RFIFOL(fd,2),RFIFOL(fd,6),RFIFOL(fd,10),RFIFOB(fd,14));
	return 0;
}
// ギルドメンバ脱退/追放通知
int intif_parse_GuildMemberLeaved(int fd)
{
	guild_member_leaved(RFIFOL(fd,2),RFIFOL(fd,6),RFIFOL(fd,10),RFIFOB(fd,14),
		RFIFOP(fd,55),RFIFOP(fd,15));
	return 0;
}

// ギルドメンバオンライン状態/Lv変更通知
int intif_parse_GuildMemberInfoShort(int fd)
{
	guild_recv_memberinfoshort(RFIFOL(fd,2),RFIFOL(fd,6),RFIFOL(fd,10),RFIFOB(fd,14),RFIFOW(fd,15));
	return 0;
}
// ギルド解散通知
int intif_parse_GuildBroken(int fd)
{
	guild_broken(RFIFOL(fd,2),RFIFOB(fd,6));
	return 0;
}

// ギルド基本情報変更通知
int intif_parse_GuildBasicInfoChanged(int fd)
{
	int type=RFIFOW(fd,8),guild_id=RFIFOL(fd,4);
	void *data=RFIFOP(fd,10);
	struct guild *g=guild_search(guild_id);
	short dw=*((short *)data);
	int dd=*((int *)data);
	if( g==NULL )
		return 0;
	switch(type){
	case GBI_EXP:			g->exp=dd; break;
	case GBI_GUILDLV:		g->guild_lv=dw; break;
	case GBI_SKILLPOINT:	g->skill_point=dd; break;
	}
	return 0;
}
// ギルドメンバ情報変更通知
int intif_parse_GuildMemberInfoChanged(int fd)
{
	int type=RFIFOW(fd,16),guild_id=RFIFOL(fd,4);
	int account_id=RFIFOL(fd,8),char_id=RFIFOL(fd,12);
	void *data=RFIFOP(fd,18);
	struct guild *g=guild_search(guild_id);
	int idx,dd=*((int *)data);
	if( g==NULL )
		return 0;
	idx=guild_getindex(g,account_id,char_id);
	switch(type){
	case GMI_POSITION:
		g->member[idx].position=dd;
		guild_memberposition_changed(g,idx,dd);
		break;
	case GMI_EXP:
		g->member[idx].exp=dd;
		break;
	}
	return 0;
}

// ギルド役職変更通知
int intif_parse_GuildPosition(int fd)
{
	if( RFIFOW(fd,2)!=sizeof(struct guild_position)+12 ){
		printf("intif: guild info : data size error\n %d %d %d",RFIFOL(fd,4),
			RFIFOW(fd,2),sizeof(struct guild_position)+12);
	}
	guild_position_changed(RFIFOL(fd,4),RFIFOL(fd,8),(struct guild_position *)RFIFOP(fd,12));
	return 0;
}
// ギルドスキル割り振り通知
int intif_parse_GuildSkillUp(int fd)
{
	guild_skillupack(RFIFOL(fd,2),RFIFOL(fd,6),RFIFOL(fd,10));
	return 0;
}
// ギルド同盟/敵対通知
int intif_parse_GuildAlliance(int fd)
{
	guild_allianceack(RFIFOL(fd,2),RFIFOL(fd,6),RFIFOL(fd,10),RFIFOL(fd,14),
		RFIFOB(fd,18),RFIFOP(fd,19),RFIFOP(fd,43));
	return 0;
}
// ギルド告知変更通知
int intif_parse_GuildNotice(int fd)
{
	guild_notice_changed(RFIFOL(fd,2),RFIFOP(fd,6),RFIFOP(fd,66));
	return 0;
}
// ギルドエンブレム変更通知
int intif_parse_GuildEmblem(int fd)
{
	guild_emblem_changed(RFIFOW(fd,2)-12,RFIFOL(fd,4),RFIFOL(fd,8),RFIFOP(fd,12));
	return 0;
}
// ギルド会話受信
int intif_parse_GuildMessage(int fd)
{
	guild_recv_message(RFIFOL(fd,4),RFIFOL(fd,8),RFIFOP(fd,12),RFIFOW(fd,2)-12);
	return 0;
}

// pet
int intif_parse_CreatePet(int fd)
{
	pet_get_egg(RFIFOL(fd,2),RFIFOL(fd,7),RFIFOB(fd,6));

	return 0;
}

int intif_parse_RecvPetData(int fd)
{
	struct s_pet p;
	int len=RFIFOW(fd,2);
	if(sizeof(struct s_pet)!=len-9) {
		printf("intif: pet data: data size error %d %d\n",sizeof(struct s_pet),len-9);
	}
	else{
		memcpy(&p,RFIFOP(fd,9),sizeof(struct s_pet));
		pet_recv_petdata(RFIFOL(fd,4),&p,RFIFOB(fd,8));
	}

	return 0;
}
int intif_parse_SavePetOk(int fd)
{
	if(RFIFOB(fd,6) == 1)
		printf("pet data save failure\n");

	return 0;
}

int intif_parse_DeletePetOk(int fd)
{
	if(RFIFOB(fd,2) == 1)
		printf("pet data delete failure\n");

	return 0;
}

//-----------------------------------------------------------------
// inter serverからの通信
// エラーがあれば0(false)を返すこと
// パケットが処理できれば1,パケット長が足りなければ2を返すこと
int intif_parse(int fd)
{
	int packet_len;
	int cmd = RFIFOW(fd,0);
	// パケットのID確認
	if(cmd<0x3800 || cmd>=0x3800+(sizeof(packet_len_table)/sizeof(packet_len_table[0])) ||
	   packet_len_table[cmd-0x3800]==0){
	   	return 0;
	}
	// パケットの長さ確認
	packet_len = packet_len_table[cmd-0x3800];
	if(packet_len==-1){
		if(RFIFOREST(fd)<4)
			return 2;
		packet_len = RFIFOW(fd,2);
	}
//	printf("intif_parse %d %x %d %d\n",fd,cmd,packet_len,RFIFOREST(fd));
	if(RFIFOREST(fd)<packet_len){
		return 2;
	}
	
	// 処理分岐
	switch(cmd){
	case 0x3800:	clif_GMmessage(NULL,RFIFOP(fd,4),packet_len-4,0); break;
	case 0x3801:	intif_parse_WisMessage(fd); break;
	case 0x3802:	intif_parse_WisEnd(fd); break;
	case 0x3810:	intif_parse_LoadStorage(fd); break;
	case 0x3811:	intif_parse_SaveStorage(fd); break;
	case 0x3820:	intif_parse_PartyCreated(fd); break;
	case 0x3821:	intif_parse_PartyInfo(fd); break;
	case 0x3822:	intif_parse_PartyMemberAdded(fd); break;
	case 0x3823:	intif_parse_PartyOptionChanged(fd); break;
	case 0x3824:	intif_parse_PartyMemberLeaved(fd); break;
	case 0x3825:	intif_parse_PartyMove(fd); break;
	case 0x3826:	intif_parse_PartyBroken(fd); break;
	case 0x3827:	intif_parse_PartyMessage(fd); break;
	case 0x3830:	intif_parse_GuildCreated(fd); break;
	case 0x3831:	intif_parse_GuildInfo(fd); break;
	case 0x3832:	intif_parse_GuildMemberAdded(fd); break;
	case 0x3834:	intif_parse_GuildMemberLeaved(fd); break;
	case 0x3835:	intif_parse_GuildMemberInfoShort(fd); break;
	case 0x3836:	intif_parse_GuildBroken(fd); break;
	case 0x3837:	intif_parse_GuildMessage(fd); break;
	case 0x3839:	intif_parse_GuildBasicInfoChanged(fd); break;
	case 0x383a:	intif_parse_GuildMemberInfoChanged(fd); break;
	case 0x383b:	intif_parse_GuildPosition(fd); break;
	case 0x383c:	intif_parse_GuildSkillUp(fd); break;
	case 0x383d:	intif_parse_GuildAlliance(fd); break;
	case 0x383e:	intif_parse_GuildNotice(fd); break;
	case 0x383f:	intif_parse_GuildEmblem(fd); break;
	case 0x3880:	intif_parse_CreatePet(fd); break;
	case 0x3881:	intif_parse_RecvPetData(fd); break;
	case 0x3882:	intif_parse_SavePetOk(fd); break;
	case 0x3883:	intif_parse_DeletePetOk(fd); break;
	default:
		printf("intif_parse : unknown packet %d %x\n",fd,RFIFOW(fd,0));
		return 0;
	}
	// パケット読み飛ばし
	RFIFOSKIP(fd,packet_len);
	
	return 1;
}



