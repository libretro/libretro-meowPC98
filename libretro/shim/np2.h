
typedef struct {
	UINT8	NOWAIT;
	UINT8	DRAW_SKIP;
	UINT8	F12KEY;
	UINT8	resume;
	UINT8	jastsnd;
} NP2OSCFG;

enum {
	FULLSCREEN_WIDTH	= 640,
	FULLSCREEN_HEIGHT	= 400
};

extern	NP2OSCFG	np2oscfg;

extern int np2_main(int argc, char *argv[]);
extern int np2_end(void);
