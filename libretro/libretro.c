#include <stdio.h>
#include <stdint.h>
#ifndef _MSC_VER
#include <stdbool.h>
#include <unistd.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <ctype.h>

#include "libretro.h"
#include "libretro_params.h"

#include "compiler.h"//required to prevent missing type errors
#include "pccore.h"
#include "cpucore.h"
#include "keystat.h"
#include "fddfile.h"
#include "newdisk.h"
#include "diskdrv.h"
#include "fontmng.h"
#include "ini.h"
#include "scrnmng.h"
#include "soundmng.h"
#include "sysmng.h"
#include "joymng.h"
#include "mousemng.h"
#include "font.h"
#include "keyboardtranslate.h"
#include "vramhdl.h"
#include "menubase.h"
#include "sysmenu.h"
#include "milstr.h"
#include "strres.h"
#include "np2.h"
#include "dosio.h"
#include "joymng.h"
#include "scrndraw.h"
#include "beep.h"
#include "pcm86.h"
#include "rhythm.h"
#include "psggen.h"
#include "opngen.h"
#include "oplgen.h"
#include "adpcm.h"

#define SOUNDRATE 44100.0
#define SNDSZ 735
signed short soundbuf[1024*2];

char RPATH[512];
char tmppath[4096]; 

static retro_log_printf_t log_cb = NULL;
static retro_video_refresh_t video_cb = NULL;
static retro_input_poll_t poll_cb = NULL;
retro_input_state_t input_cb = NULL;
static retro_environment_t environ_cb = NULL;

uint16_t   FrameBuffer[LR_SCREENWIDTH * LR_SCREENHEIGHT];
uint16_t   GuiBuffer[LR_SCREENWIDTH * LR_SCREENHEIGHT]; //menu surf

retro_audio_sample_batch_t audio_batch_cb = NULL;

static char CMDFILE[512];

int PAS=3;
bool GUIMOUSE =true;

bool did_reset;

long frame=0;
unsigned long  Ktime=0 , LastFPSTime=0;
int slowdown=0;

void gui_delay_events(void)
{
   	Ktime =(unsigned long)  GETTICK();

   	if(Ktime - LastFPSTime >= 1000/50)
   	{
      		slowdown=0;
      		frame++; 
      		LastFPSTime = Ktime;
   	}
}

int loadcmdfile(char *argv)
{
   int res=0;

   FILE *fp = fopen(argv,"r");

   if( fp != NULL )
   {
      if ( fgets (CMDFILE , 512 , fp) != NULL )
         res=1;	
      fclose (fp);
   }

   return res;
}

int HandleExtension(char *path,char *ext)
{
   int len = strlen(path);

   if (len >= 4 &&
         path[len-4] == '.' &&
         path[len-3] == ext[0] &&
         path[len-2] == ext[1] &&
         path[len-1] == ext[2])
   {
      return 1;
   }

   return 0;
}
//Args for experimental_cmdline
static char ARGUV[64][1024];
static unsigned char ARGUC=0;

// Args for Core
static char XARGV[64][1024];
static const char* xargv_cmd[64];
int PARAMCOUNT=0;

extern int cmain(int argc, char *argv[]);

void parse_cmdline( const char *argv );

void Add_Option(const char* option)
{
   static int first=0;

   if(first==0)
   {
      PARAMCOUNT=0;	
      first++;
   }

   sprintf(XARGV[PARAMCOUNT++],"%s\0",option);
}

int pre_main(const char *argv)
{
   int i=0;
   int Only1Arg;

   if (strlen(argv) > strlen("cmd"))
   {
      if( HandleExtension((char*)argv,"cmd") || HandleExtension((char*)argv,"CMD"))
         i=loadcmdfile((char*)argv);     
   }

   if(i==1)
      parse_cmdline(CMDFILE);      
   else
      parse_cmdline(argv); 

   Only1Arg = (strcmp(ARGUV[0],"np21") == 0) ? 0 : 1;

   for (i = 0; i<64; i++)
      xargv_cmd[i] = NULL;


   if(Only1Arg)
   {  
      int cfgload=0;

      Add_Option("np21");

      if(cfgload==0)
      {

      }

      Add_Option(RPATH);
   }
   else
   { // Pass all cmdline args
      for(i = 0; i < ARGUC; i++)
         Add_Option(ARGUV[i]);
   }

   for (i = 0; i < PARAMCOUNT; i++)
   {
      xargv_cmd[i] = (char*)(XARGV[i]);
      printf("arg_%d:%s\n",i,xargv_cmd[i]);
   }

   dosio_init();
   file_setcd(tmppath);

   i=np2_main(PARAMCOUNT,( char **)xargv_cmd); 

   xargv_cmd[PARAMCOUNT - 2] = NULL;

   return 0;
}

void parse_cmdline(const char *argv)
{
   char *p,*p2,*start_of_word;
   int c,c2;
   static char buffer[512*4];
   enum states { DULL, IN_WORD, IN_STRING } state = DULL;

   strcpy(buffer,argv);
   strcat(buffer," \0");

   for (p = buffer; *p != '\0'; p++)
   {
      c = (unsigned char) *p; /* convert to unsigned char for is* functions */
      switch (state)
      {
         case DULL: /* not in a word, not in a double quoted string */
            if (isspace(c)) /* still not in a word, so ignore this char */
               continue;
            /* not a space -- if it's a double quote we go to IN_STRING, else to IN_WORD */
            if (c == '"')
            {
               state = IN_STRING;
               start_of_word = p + 1; /* word starts at *next* char, not this one */
               continue;
            }
            state = IN_WORD;
            start_of_word = p; /* word starts here */
            continue;
         case IN_STRING:
            /* we're in a double quoted string, so keep going until we hit a close " */
            if (c == '"')
            {
               /* word goes from start_of_word to p-1 */
               //... do something with the word ...
               for (c2 = 0,p2 = start_of_word; p2 < p; p2++, c2++)
                  ARGUV[ARGUC][c2] = (unsigned char) *p2;
               ARGUC++; 

               state = DULL; /* back to "not in word, not in string" state */
            }
            continue; /* either still IN_STRING or we handled the end above */
         case IN_WORD:
            /* we're in a word, so keep going until we get to a space */
            if (isspace(c))
            {
               /* word goes from start_of_word to p-1 */
               //... do something with the word ...
               for (c2 = 0,p2 = start_of_word; p2 <p; p2++,c2++)
                  ARGUV[ARGUC][c2] = (unsigned char) *p2;
               ARGUC++; 

               state = DULL; /* back to "not in word, not in string" state */
            }
            continue; /* either still IN_WORD or we handled the end above */
      }	
   }
}

static const char *cross[] = {
  "X                               ",
  "XX                              ",
  "X.X                             ",
  "X..X                            ",
  "X...X                           ",
  "X....X                          ",
  "X.....X                         ",
  "X......X                        ",
  "X.......X                       ",
  "X........X                      ",
  "X.....XXXXX                     ",
  "X..X..X                         ",
  "X.X X..X                        ",
  "XX  X..X                        ",
  "X    X..X                       ",
  "     X..X                       ",
  "      X..X                      ",
  "      X..X                      ",
  "       XX                       ",
  "                                ",
};

void DrawPointBmp(unsigned short *buffer,int x, int y, unsigned short color)
{
   int idx;

   idx=x+y*LR_SCREENWIDTH;
   buffer[idx]=color;	
}


void draw_cross(int x,int y) {

	int i,j,idx;
	int dx=32,dy=20;
	unsigned  short color;

	for(j=y;j<y+dy;j++){
		idx=0;
		for(i=x;i<x+dx;i++){
			if(cross[j-y][idx]=='.')DrawPointBmp(FrameBuffer,i,j,0xffff);
			else if(cross[j-y][idx]=='X')DrawPointBmp(FrameBuffer,i,j,0);
			idx++;			
		}
	}

}

static int lastx=320,lasty=200;
static int menukey=0;
static int menu_active=0;

void updateInput(){

   static int mposx=LR_SCREENWIDTH/2,mposy=LR_SCREENHEIGHT/2;

   poll_cb();

   joymng_sync();

   uint32_t i;
   for (i=0; i < keys_needed; i++)
      if (input_cb(0, RETRO_DEVICE_KEYBOARD, 0, keys_to_poll[i])){
         send_libretro_key_down(keys_to_poll[i]);
      }
      else {
         send_libretro_key_up(keys_to_poll[i]);
      }

   if ((input_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_F11) || 
            input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2)) && menukey==0){

      menukey=1; 

      if (menuvram == NULL) {
         sysmenu_menuopen(0, 0, 0);
         mposx=0;mposy=0;
         lastx=0;lasty=0;
         mousemng_disable(MOUSEPROC_SYSTEM);
         menu_active=1;
      } else {
         menubase_close();
         mousemng_enable(MOUSEPROC_SYSTEM);
         memset(GuiBuffer,0,LR_SCREENWIDTH*LR_SCREENHEIGHT*2);
         scrndraw_redraw();
         menu_active=0;
      }
   } else if ( !(input_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_F11) || 
            input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2)) && menukey==1)
      menukey=0;

   if (menuvram == NULL && menu_active==1){
      menubase_close();
      menu_active==0;
      mousemng_enable(MOUSEPROC_SYSTEM);
      memset(GuiBuffer,0,LR_SCREENWIDTH*LR_SCREENHEIGHT*2);
      scrndraw_redraw();
   }

   static int mbL = 0, mbR = 0;
   int mouse_x=0,mouse_y=0,mouse_l,mouse_r;

   if(GUIMOUSE){
      mouse_x = input_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
      mouse_y = input_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
   }
   else {
      if (input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT))
         mouse_x += PAS;
      if (input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT))
         mouse_x -= PAS;
      if (input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN))
         mouse_y += PAS;
      if (input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP))
         mouse_y -= PAS;
   }

   if (!mousemng.flag){			
	mousemng_sync(mouse_x,mouse_y);
   }

   mposx+=mouse_x;if(mposx<0)mposx=0;if(mposx>=LR_SCREENWIDTH)mposx=LR_SCREENWIDTH-1;
   mposy+=mouse_y;if(mposy<0)mposy=0;if(mposy>=LR_SCREENHEIGHT)mposy=LR_SCREENHEIGHT-1;

   if(lastx!=mposx || lasty!=mposy)
	if (menuvram == NULL) {
	}
	else {
		menubase_moving(mposx, mposy, 0);
	}

   if(GUIMOUSE){
   	mouse_l = input_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT);
   	mouse_r = input_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT);
   }
   else {
        mouse_l = input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
        mouse_r = input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   }
	      
   if(mbL==0 && mouse_l)
   {
      	mbL=1;		

	if (menuvram != NULL)
	{
		menubase_moving(mposx, mposy, 1);
	}
	else mousemng_buttonevent(MOUSEMNG_LEFTDOWN);
			
   }
   else if(mbL==1 && !mouse_l)
   {
   	mbL=0;

	if (!mousemng_buttonevent(MOUSEMNG_LEFTUP))
	{
		if (menuvram != NULL)
		{
			menubase_moving(mposx, mposy, 2);
		}
		else
		{
			sysmenu_menuopen(0, mposx, mposy);
		}
	}

   }

   if(mbR==0 && mouse_r){
      	mbR=1;		
	mousemng_buttonevent(MOUSEMNG_RIGHTDOWN);
   }
   else if(mbR==1 && !mouse_r)
   {
   	mbR=0;
	mousemng_buttonevent(MOUSEMNG_RIGHTUP);
   }

   lastx=mposx;lasty=mposy;

}

void *retro_get_memory_data(unsigned type)
{
   if ( type == RETRO_MEMORY_SYSTEM_RAM )
      return CPU_EXTMEM;
   return NULL;
}
size_t retro_get_memory_size(unsigned type)
{
   if ( type == RETRO_MEMORY_SYSTEM_RAM )
      return CPU_EXTMEMSIZE;
   return 0;
}
//dummy functions
void retro_set_audio_sample(retro_audio_sample_t cb){}
bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info){return false;}
void retro_unload_game (void){}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
   poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
   input_cb = cb;
}

void retro_set_environment(retro_environment_t cb)
{
   struct retro_log_callback logging;
   
   environ_cb = cb;
   
   //bool no_rom = !LR_REQUIRESROM;
   //environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_rom);

   struct retro_variable variables[] = {
      { "np2_model" , "PC Model (Restart); VX|EPSON|VM" },
      { "np2_clk_base" , "CPU Base Clock (Restart); 2.4576 MHz|1.9968 MHz" },
      { "np2_clk_mult" , "CPU Clock Multiplier (Restart); 4|5|6|7|8|9|10|11|12|13|14|15|16|17|18|19|20|21|22|23|24|25|26|27|28|29|30|31|32|1|2|3" },
      { "np2_ExMemory" , "RAM Size (Restart); 2|3|4|5|6|7|8|9|10|11|12|13|14|15|16|24|32|48|64|1" },
      { "np2_skipline" , "Skipline Revisions; Full 255 lines|ON|OFF" },
      { "np2_SNDboard" , "Sound Board (Restart); PC9801-86|PC9801-26K + 86|PC9801-86 + Chibi-Oto|PC9801-118|Speak Board|Spark Board|Sound Orchestra|Sound Orchestra-V|AMD-98|None|PC9801-14|PC9801-26K" },
      { "np2_jast_snd" , "JastSound; OFF|ON" },
      { "np2_volume_F" , "Volume FM; 64|68|72|76|80|84|88|92|96|100|104|108|112|116|120|124|128|0|4|8|12|16|20|24|28|32|36|40|44|48|52|56|60" },
      { "np2_volume_S" , "Volume SSG; 64|68|72|76|80|84|88|92|96|100|104|108|112|116|120|124|128|0|4|8|12|16|20|24|28|32|36|40|44|48|52|56|60" },
      { "np2_volume_A" , "Volume ADPCM; 64|68|72|76|80|84|88|92|96|100|104|108|112|116|120|124|128|0|4|8|12|16|20|24|28|32|36|40|44|48|52|56|60" },
      { "np2_volume_P" , "Volume PCM; 64|68|72|76|80|84|88|92|96|100|104|108|112|116|120|124|128|0|4|8|12|16|20|24|28|32|36|40|44|48|52|56|60" },
      { "np2_volume_R" , "Volume RHYTHM; 64|68|72|76|80|84|88|92|96|100|104|108|112|116|120|124|128|0|4|8|12|16|20|24|28|32|36|40|44|48|52|56|60" },
      { "np2_Seek_Snd" , "Floppy Seek Sound (Restart); OFF|ON" },
      { "np2_Seek_Vol" , "Volume Floppy Seek (Restart); 80|84|88|92|96|100|104|108|112|116|120|124|128|0|4|8|12|16|20|24|28|32|36|40|44|48|52|56|60|64|68|72|76" },
      { "np2_BEEP_vol" , "Volume Beep; 3|0|1|2" },
      { "np2_GUI_controller" , "Gui Controller; MOUSE|JOY0" },
      { "np2_GUIJOY_PAS" , "Gui Joy0 PAS; 3|4|5|1|2" },
      { NULL, NULL },
   };

   if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
      log_cb = logging.log;
   else
      log_cb = NULL;
   
   cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
}

static void update_variables(void)
{
   struct retro_variable var = {0};

   var.key = "np2_model";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "VX") == 0)
         milstr_ncpy(np2cfg.model, str_VX, 3);
      else if (strcmp(var.value, "EPSON") == 0)
         milstr_ncpy(np2cfg.model, str_EPSON, 6);
      else if (strcmp(var.value, "VM") == 0)
         milstr_ncpy(np2cfg.model, str_VM, 3);
   }

   var.key = "np2_clk_base";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "1.9968 MHz") == 0)
         np2cfg.baseclock = 1996800;
      else
         np2cfg.baseclock = 2457600;
   }

   var.key = "np2_clk_mult";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      np2cfg.multiple = atoi(var.value);
   }

   var.key = "np2_ExMemory";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      np2cfg.EXTMEM = atoi(var.value);
   }

   var.key = "np2_skipline";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "OFF") == 0)
         np2cfg.skipline = false;
      else if (strcmp(var.value, "ON") == 0)
         np2cfg.skipline = true;
      else if (strcmp(var.value, "Full 255 lines") == 0){
         np2cfg.skiplight = 255;
         np2cfg.skipline = true;
      }
	  scrndraw_redraw();
   }

   var.key = "np2_SNDboard";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "PC9801-86") == 0)
         np2cfg.SOUND_SW = 0x04;
      else if (strcmp(var.value, "PC9801-26K + 86") == 0)
         np2cfg.SOUND_SW = 0x06;
      else if (strcmp(var.value, "PC9801-86 + Chibi-Oto") == 0)
         np2cfg.SOUND_SW = 0x14;
      else if (strcmp(var.value, "PC9801-118") == 0)
         np2cfg.SOUND_SW = 0x08;
      else if (strcmp(var.value, "Speak Board") == 0)
         np2cfg.SOUND_SW = 0x20;
      else if (strcmp(var.value, "Spark Board") == 0)
         np2cfg.SOUND_SW = 0x40;
      else if (strcmp(var.value, "Sound Orchestra") == 0)
         np2cfg.SOUND_SW = 0x32;
      else if (strcmp(var.value, "Sound Orchestra-V") == 0)
         np2cfg.SOUND_SW = 0x82;
      else if (strcmp(var.value, "AMD-98") == 0)
         np2cfg.SOUND_SW = 0x80;
      else if (strcmp(var.value, "None") == 0)
         np2cfg.SOUND_SW = 0x00;
      else if (strcmp(var.value, "PC9801-14") == 0)
         np2cfg.SOUND_SW = 0x01;
      else if (strcmp(var.value, "PC9801-26K") == 0)
         np2cfg.SOUND_SW = 0x02;
   }

   var.key = "np2_jast_snd";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "OFF") == 0)
         np2oscfg.jastsnd = 0;
      else if (strcmp(var.value, "ON") == 0)
         np2oscfg.jastsnd = 1;
   }

   var.key = "np2_volume_F";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      np2cfg.vol_fm = atoi(var.value);
      opngen_setvol(np2cfg.vol_fm);
      oplgen_setvol(np2cfg.vol_fm);
   }

   var.key = "np2_volume_S";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      np2cfg.vol_ssg = atoi(var.value);
      psggen_setvol(np2cfg.vol_ssg);
   }

   var.key = "np2_volume_A";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      np2cfg.vol_adpcm = atoi(var.value);
      adpcm_setvol(np2cfg.vol_adpcm);
   }

   var.key = "np2_volume_P";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      np2cfg.vol_pcm = atoi(var.value);
      pcm86gen_setvol(np2cfg.vol_pcm);
   }

   var.key = "np2_volume_R";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      np2cfg.vol_rhythm = atoi(var.value);
      rhythm_setvol(np2cfg.vol_rhythm);
   }

   var.key = "np2_Seek_Snd";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "OFF") == 0)
         np2cfg.MOTOR = false;
      else if (strcmp(var.value, "ON") == 0)
         np2cfg.MOTOR = true;
   }

   var.key = "np2_Seek_Vol";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      np2cfg.MOTORVOL = atoi(var.value);
   }

   var.key = "np2_BEEP_vol";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      np2cfg.BEEP_VOL = atoi(var.value);
      beep_setvol(np2cfg.BEEP_VOL);
   }

   var.key = "np2_GUI_controller";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "MOUSE") == 0)
         GUIMOUSE= true;
      else if (strcmp(var.value, "JOY0") == 0)
         GUIMOUSE = false;
   }

   var.key = "np2_GUIJOY_PAS";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      PAS = atoi(var.value);      
   }

   initsave();

}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->need_fullpath    = LR_NEEDFILEPATH;
   info->valid_extensions = LR_VALIDFILEEXT;
   info->library_version  = LR_LIBVERSION;
   info->library_name     = LR_CORENAME;
   info->block_extract    = LR_BLOCKARCEXTRACT;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   info->geometry.base_width   = LR_SCREENWIDTH;
   info->geometry.base_height  = LR_SCREENHEIGHT;
   info->geometry.max_width    = LR_SCREENWIDTH;
   info->geometry.max_height   = LR_SCREENHEIGHT;
   info->geometry.aspect_ratio = LR_SCREENASPECT;
   info->timing.fps            = LR_SCREENFPS;
   info->timing.sample_rate    = LR_SOUNDRATE;
}

void retro_init (void)
{
   enum retro_pixel_format rgb565;
   

   rgb565 = RETRO_PIXEL_FORMAT_RGB565;
   if(environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &rgb565) && log_cb)
         log_cb(RETRO_LOG_INFO, "Frontend supports RGB565 - will use that instead of XRGB1555.\n");

   update_variables();

   init_lr_key_to_pc98();
}

void retro_deinit(void)
{
   np2_end();
}

void retro_reset (void)
{
   pccore_reset();
   did_reset = true;
}
extern  void playretro();

static int firstcall=1;

void retro_run (void)
{
   if(firstcall)
   {
      pre_main(RPATH);
      update_variables();
      pccore_cfgupdate();
      pccore_reset();
      mousemng_enable(MOUSEPROC_SYSTEM);
      firstcall=0;
      printf("INIT done\n");
      return;
   }

   bool updated = false;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
   {
      update_variables();
   }

   if (did_reset){
      pccore_cfgupdate();
      pccore_reset();
      did_reset = false;
   }

   if (menuvram != NULL){
	slowdown=1;
	gui_delay_events();
   }

   updateInput();

   if (menuvram != NULL){
	memcpy(FrameBuffer,GuiBuffer,LR_SCREENWIDTH*LR_SCREENHEIGHT*2);
	draw_cross(lastx,lasty);
   }
   else {
   	//emulate 1 frame
   	pccore_exec(true /*draw*/);
   	sound_play_cb(NULL, NULL,SNDSZ*4);
   }

   video_cb(FrameBuffer, LR_SCREENWIDTH, LR_SCREENHEIGHT, LR_SCREENWIDTH * 2/*Pitch*/);
}

size_t retro_serialize_size (void)
{
   //no savestates on this core
   return 0;
}

bool retro_serialize(void *data, size_t size)
{
   //no savestates on this core
   return false;
}

bool retro_unserialize(const void * data, size_t size)
{
   //no savestates on this core
   return false;
}

void retro_cheat_reset(void)
{
   //no cheats on this core
} 

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   //no cheats on this core
}

bool retro_load_game(const struct retro_game_info *game)
{
   
   //get system dir
   const char* syspath = 0;
   char np2path[4096];
   bool load_floppy=false;
   bool worked = environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &syspath);
   if(!worked)abort();
   
   strcpy(np2path, syspath);

#ifdef _WIN32
   strcat(np2path, "\\np2");
#else 
   strcat(np2path, "/np2");
#endif

   sprintf(tmppath,"%s%c",np2path,G_DIR_SEPARATOR);

   np2cfg.delayms = 0;
   
   sprintf(np2cfg.fontfile,"%s%cfont.bmp",np2path,G_DIR_SEPARATOR);
  
   sprintf(np2cfg.biospath,"%s%c",np2path,G_DIR_SEPARATOR);

   strcpy(RPATH,game->path);

   return true;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   (void)port;
   (void)device;
}

unsigned retro_get_region (void)
{ 
   return RETRO_REGION_NTSC;
}
