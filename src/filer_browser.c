//--------------------------------------------------------------
//File name:   filer_browser.c
//--------------------------------------------------------------
#include "launchelf.h"
#include "gui_texteditor.h"
#include "filer_actions.h"
#include "filer_shared.h"
#include "gui_hdd0_format.h"
#include "init.h"

#define SOURCE_DEVICE_WAIT_INTERVAL_MS 1000
#define SOURCE_DEVICE_WAIT_TIMEOUT_MS 6000

static int isHddBrowserPath(const char *path)
{
	return (!strncmp(path, "hdd", 3) && path[3] >= '0' && path[3] <= '9' && path[4] == ':' && path[5] == '/');
}

static int isHddRootPath(const char *path)
{
	return (isHddBrowserPath(path) && path[6] == '\0');
}

static int isGenericUsbRootPath(const char *path)
{
	return (!strcmp(path, "usb:") || !strcmp(path, "usb:/"));
}

static const char *getUsbRootDeviceLabel(char unit)
{
	static char label[8];

	if ((unit < '0') || (unit > '9'))
		return NULL;

	sprintf(label, "U盘%c", unit);
	return label;
}

static const char *getRootDeviceLabel(const char *name)
{
	if (!strcmp(name, "mc0:"))
		return "记忆卡0";
	if (!strcmp(name, "mc1:"))
		return "记忆卡1";
	if (!strcmp(name, "mass:"))
		return "U盘";
	if (!strncmp(name, "mass", 4) && name[4] >= '0' && name[4] <= '9' && name[5] == ':' && name[6] == '\0')
		return getUsbRootDeviceLabel(name[4]);
	if (!strcmp(name, "usb:"))
		return "U盘";
	if (!strncmp(name, "usb", 3) && name[3] >= '0' && name[3] <= '9' && name[4] == ':' && name[5] == '\0')
		return getUsbRootDeviceLabel(name[3]);
#ifdef MMCE
	if (!strcmp(name, "mmce0:"))
		return "虚拟记忆卡0";
	if (!strcmp(name, "mmce1:"))
		return "虚拟记忆卡1";
#endif
#ifdef MX4SIO
	if (!strcmp(name, "mx4sio:"))
		return "MX4SIO卡";
#endif
	if (!strcmp(name, "hdd0:"))
		return "内置apa硬盘";
	if (!strcmp(name, "hdd1:"))
		return "内置apa硬盘1";
#ifdef EXFAT
	if (!strcmp(name, "ata:"))
		return "内置exfat硬盘";
	if (!strcmp(name, "ata0:"))
		return "内置exfat硬盘";
	if (!strcmp(name, "ata1:"))
		return "内置exfat硬盘1";
#endif
	if (!strcmp(name, "cdfs:"))
		return "光盘";
#ifdef XFROM
	if (!strcmp(name, "xfrom0:") || !strcmp(name, "xfrom:"))
		return "xfrom:/";
#endif
#ifdef DVRP
	if (!strcmp(name, "dvr_hdd0:"))
		return "dvr:/";
#endif
#ifdef ETH
	if (!strcmp(name, "host:"))
		return "host:/";
#endif
#ifdef UDPFS
	if (!strcmp(name, "udpfs:"))
		return "网络udpfs";
#endif
#ifdef SMB
	if (!strcmp(name, "smb:"))
		return "网络SMB共享";
#endif
	if (!strcmp(name, LNG(MISC)))
		return "MISC/";

	return NULL;
}

static void waitUntilTimer(u64 end_time)
{
	while (Timer() < end_time) {
	}
}

static int probeDirectory(const char *path)
{
	char probe_path[MAX_PATH];
	int fd;

	if (path == NULL || path[0] == '\0')
		return FALSE;

	snprintf(probe_path, sizeof(probe_path), "%s", path);
	fd = genDopen(probe_path);
	if (fd < 0)
		return FALSE;
	genDclose(fd);
	return TRUE;
}

static void markMx4sioDestinationAfterWrite(const char *path)
{
	if (path == NULL || strncmp(path, "mx4sio", 6))
		return;

	discardNextMx4sioRootListing(path);
}

static void makeDeviceRootPath(const char *path, char *root, int root_size)
{
	const char *separator;
	int len;

	if (root_size <= 0)
		return;
	root[0] = '\0';
	if (path == NULL)
		return;

	separator = strchr(path, ':');
	if (separator == NULL)
		return;

	len = (int)(separator - path) + 1;
	if (len >= root_size)
		len = root_size - 1;
	memcpy(root, path, len);
	root[len] = '\0';

#if defined(ETH) || defined(UDPFS)
	if (!strncmp(root, "host:", 5))
		return;
#endif

	if (len + 1 < root_size) {
		root[len++] = '/';
		root[len] = '\0';
	}
}

static int clipboardHddSourceReady(void)
{
	char party[MAX_NAME], dir[MAX_PATH];
	int pfs_ix;

	if (getHddParty(clipPath, NULL, party, dir) < 0)
		return FALSE;
	if (!ensurePathDeviceStackReady(clipPath))
		return FALSE;
	pfs_ix = mountParty(party);
	return (pfs_ix >= 0);
}

#ifdef DVRP
static int clipboardDvrHddSourceReady(void)
{
	char party[MAX_NAME], dir[MAX_PATH];
	int pfs_ix;

	if (getHddDVRPParty(clipPath, NULL, party, dir) < 0)
		return FALSE;
	if (!ensurePathDeviceStackReady(clipPath))
		return FALSE;
	pfs_ix = mountDVRPParty(party);
	return (pfs_ix >= 0);
}
#endif

static int clipboardSourceDeviceReady(void)
{
	char fixed_path[MAX_PATH], root_path[MAX_PATH];

	if (clipPath[0] == '\0')
		return FALSE;
	if (!strncmp(clipPath, "mc", 2))
		return TRUE;
	if (!strncmp(clipPath, "vmc", 3)) {
		int vmc_index = clipPath[3] - '0';

		return (vmc_index >= 0 && vmc_index < 2 && vmcMounted[vmc_index]);
	}
	if (isHddBrowserPath(clipPath))
		return clipboardHddSourceReady();
#ifdef DVRP
	if (!strncmp(clipPath, "dvr_hdd", 7))
		return clipboardDvrHddSourceReady();
#endif

	if (!ensurePathDeviceStackReady(clipPath))
		return FALSE;
	if (genFixPath(clipPath, fixed_path) < 0)
		return FALSE;
	makeDeviceRootPath(fixed_path, root_path, sizeof(root_path));
	if (probeDirectory(root_path))
		return TRUE;
	return probeDirectory(fixed_path);
}

static int waitForClipboardSourceDevice(void)
{
	u64 deadline, next_check;

	if (clipIopResetGeneration == getIopResetGeneration())
		return 0;

	deadline = Timer() + SOURCE_DEVICE_WAIT_TIMEOUT_MS;
	while (1) {
		if (clipboardSourceDeviceReady()) {
			clipIopResetGeneration = getIopResetGeneration();
			return 0;
		}
		if (Timer() >= deadline)
			break;

		drawMsg(LNG(Pasting));
		next_check = Timer() + SOURCE_DEVICE_WAIT_INTERVAL_MS;
		if (next_check > deadline)
			next_check = deadline;
		waitUntilTimer(next_check);
	}

	return -1;
}

static void formatBrowserPathForDisplay(const char *path, char *display_path)
{
	const char *partition;
	const char *subpath;
	const char *sep;
	int part_len;

	if (!strncmp(path, "mass", 4)) {
		snprintf(display_path, MAX_PATH, "usb%s", path + 4);
		return;
	}

	if (isHddBrowserPath(path)) {
		char hdd_device[6];

		memcpy(hdd_device, path, 5);
		hdd_device[5] = '\0';
		partition = path + 6; // after "hdd0:/"
		if (partition[0] == '\0') {
			snprintf(display_path, MAX_PATH, "%s", path);
			return;
		}

		sep = strchr(partition, '/');
		if (sep == NULL) {
			part_len = (int)strlen(partition);
			subpath = "/";
		} else {
			part_len = (int)(sep - partition);
			subpath = sep;
		}

		if (part_len > 0) {
			snprintf(display_path, MAX_PATH, "%s%.*s:pfs:%s", hdd_device, part_len, partition, subpath);
			return;
		}
	}

#ifdef DVRP
	if (!strncmp(path, "dvr_hdd0:/", 10)) {
		partition = path + 10; // after "dvr_hdd0:/"
		if (partition[0] == '\0') {
			snprintf(display_path, MAX_PATH, "%s", path);
			return;
		}

		sep = strchr(partition, '/');
		if (sep == NULL) {
			part_len = (int)strlen(partition);
			subpath = "/";
		} else {
			part_len = (int)(sep - partition);
			subpath = sep;
		}

		if (part_len > 0) {
			snprintf(display_path, MAX_PATH, "dvr_hdd0:%.*s:pfs:%s", part_len, partition, subpath);
			return;
		}
	}
#endif

	snprintf(display_path, MAX_PATH, "%s", path);
}

static int isTitleCfgPathEligible(const char *path, int menu_disabled)
{
	return ((!strncmp(path, "mass", 4)) ||
	        (!strncmp(path, "usb", 3)) ||
#ifdef MMCE
	        (!strncmp(path, "mmce", 4)) ||
#endif
#ifdef MX4SIO
	        (!strncmp(path, "mx4sio", 6)) ||
#endif
	        (!strncmp(path, "ata", 3)) ||
	        (isHddBrowserPath(path) && !menu_disabled));
}

enum {
	PSU_ACTION_NONE = 0,
	PSU_ACTION_CREATE,
	PSU_ACTION_EXTRACT
};

static int isMcLikePath(const char *path)
{
	return (!strncmp(path, "mc", 2) || !strncmp(path, "vmc", 3));
}

static int classifyPsuAction(const char *destPath)
{
	int i;
	int all_psu = 1;
	int all_dirs = 1;
	int src_is_mc_like;

	if (nclipFiles <= 0)
		return PSU_ACTION_NONE;

	src_is_mc_like = isMcLikePath(clipPath);
	for (i = 0; i < nclipFiles; i++) {
		if (clipFiles[i].stats.AttrFile & sceMcFileAttrSubdir) {
			all_psu = 0;
			continue;
		}

		all_dirs = 0;
		if (!genCmpFileExt(clipFiles[i].name, "PSU"))
			all_psu = 0;
	}

	if (all_psu)
		return PSU_ACTION_EXTRACT;

	if (all_dirs && src_is_mc_like && destPath && destPath[0] != '\0' && !isMcLikePath(destPath))
		return PSU_ACTION_CREATE;

	return PSU_ACTION_NONE;
}

static int menu(const char *path, FILEINFO *file)
{
	u64 color;
	char enable[NUM_MENU], tmp[80];
	const char *psu_action_label;
	int x, y, i, sel;
	int event, post_event = 0;
	int menu_disabled = 0;
	int write_disabled = 0;
	int psu_action;

	psu_action = classifyPsuAction(path);
	if (psu_action == PSU_ACTION_EXTRACT)
		psu_action_label = LNG(Extract_PSU);
	else if (psu_action == PSU_ACTION_CREATE)
		psu_action_label = LNG(Create_PSU);
	else
		psu_action_label = LNG(psuAction);

	int menu_len = strlen(LNG(Copy)) > strlen(LNG(Cut)) ?
	                   strlen(LNG(Copy)) :
	                   strlen(LNG(Cut));
	menu_len = strlen(LNG(Paste)) > menu_len ? strlen(LNG(Paste)) : menu_len;
	menu_len = strlen(LNG(Delete)) > menu_len ? strlen(LNG(Delete)) : menu_len;
	menu_len = strlen(LNG(Rename)) > menu_len ? strlen(LNG(Rename)) : menu_len;
	menu_len = strlen(LNG(New_Dir)) > menu_len ? strlen(LNG(New_Dir)) : menu_len;
	menu_len = strlen(LNG(Get_Size)) > menu_len ? strlen(LNG(Get_Size)) : menu_len;
	menu_len = strlen(LNG(TextEditor)) > menu_len ? strlen(LNG(TextEditor)) : menu_len;
	menu_len = strlen(LNG(Launch_With_Args)) > menu_len ? strlen(LNG(Launch_With_Args)) : menu_len;
	menu_len = strlen(psu_action_label) > menu_len ? strlen(psu_action_label) : menu_len;
	menu_len = strlen(LNG(time_manip)) > menu_len ? strlen(LNG(time_manip)) : menu_len;
	menu_len = strlen(LNG(title_cfg)) > menu_len ? strlen(LNG(title_cfg)) : menu_len;
	menu_len = (strlen(LNG(Mount)) + 6) > menu_len ? (strlen(LNG(Mount)) + 6) : menu_len;
	

	int menu_ch_w = menu_len + 1;                                 //Total characters in longest menu string
	int menu_ch_h = NUM_MENU;                                     //Total number of menu lines
	int mSprite_Y1 = 64;                                          //Top edge of sprite
	int mSprite_X2 = SCREEN_WIDTH - 35;                           //Right edge of sprite
	int mSprite_X1 = mSprite_X2 - (menu_ch_w + 3) * FONT_WIDTH;   //Left edge of sprite
	int mSprite_Y2 = mSprite_Y1 + (menu_ch_h + 1) * FONT_HEIGHT;  //Bottom edge of sprite

	memset(enable, TRUE, NUM_MENU);  //Assume that all menu items are legal by default

	//identify cases where write access is illegal, and disable menu items accordingly
	if ((!strncmp(path, "cdfs", 4))  //Writing is always illegal for CDVD drive
#if defined(ETH) && defined(UDPFS)
	    || ((!strncmp(path, "host", 4) || !strncmp(path, "udpfs", 5))
#elif defined(ETH)
	    || ((!strncmp(path, "host", 4))
#elif defined(UDPFS)
	    || ((!strncmp(path, "udpfs", 5))
#endif
#if defined(ETH) || defined(UDPFS)
	        && ((!setting->HOSTwrite)                         //host/udpfs writing is illegal if not enabled in CNF
	            || (host_elflist && !strcmp(path, "host:/"))  //it's also illegal in elflist.txt
	            ))
#endif
				)
		write_disabled = 1;

	if ((isHddRootPath(path) || !strcmp(path, "dvr_hdd0:/")) || path[0] == 0)  //No menu cmds in partition/device lists
		menu_disabled = 1;

	if (menu_disabled) {
		enable[COPY] = FALSE;
		enable[MOUNTVMC0] = FALSE;
		enable[MOUNTVMC1] = FALSE;
		enable[GETSIZE] = FALSE;
	}
//#ifdef TMANIP
	if (                                                        //if
	    (file->stats.AttrFile & sceMcFileAttrSubdir) &&         //pointing to a folder
	    (strcmp(file->name, "..")) &&                           //it isnt the ".." option
	    ((!strcmp(path, "mc0:/")) || (!strcmp(path, "mc1:/")))  //we're on Memory card roots
	) {
		enable[TIMEMANIP] = TRUE;
	} else {
		enable[TIMEMANIP] = FALSE;
	} 
//#endif //TMANIP
	if (genCmpFileExt(file->name, "ELF") && isTitleCfgPathEligible(path, menu_disabled))
		enable[TITLE_CFG] = TRUE;
	else
		enable[TITLE_CFG] = FALSE;

	enable[OPEN_TEXTEDITOR] = canOpenInTextEditor(path, file);
	enable[LAUNCH_ELF_ARGS] = enable[OPEN_TEXTEDITOR];


	if (write_disabled || menu_disabled) {
		enable[CUT] = FALSE;
		enable[PASTE] = FALSE;
		enable[PSUPASTE] = FALSE;
		enable[DELETE] = FALSE;
		enable[RENAME] = FALSE;
		enable[NEWDIR] = FALSE;
		enable[NEWICON] = FALSE;
		enable[TITLE_CFG] = FALSE;
	}

	if (nmarks == 0) {
		if (!strcmp(file->name, "..")) {
			enable[COPY] = FALSE;
			enable[CUT] = FALSE;
			enable[DELETE] = FALSE;
			enable[RENAME] = FALSE;
			enable[GETSIZE] = FALSE;
		}
		if (filerIsExploitProtectedPath(path, file))
			enable[RENAME] = FALSE;
	} else {
		enable[RENAME] = FALSE;
	}

	if ((file->stats.AttrFile & sceMcFileAttrSubdir) || !strncmp(path, "vmc", 3) || !strncmp(path, "mc", 2)) {
		enable[MOUNTVMC0] = FALSE;  //forbid insane VMC mounting
		enable[MOUNTVMC1] = FALSE;  //forbid insane VMC mounting
	}

	if (nclipFiles == 0) {
		//Nothing in clipboard
		enable[PASTE] = FALSE;
		enable[PSUPASTE] = FALSE;
	} else {
		//Something in clipboard
		enable[PSUPASTE] = (enable[PSUPASTE] && (psu_action != PSU_ACTION_NONE));
	}

	for (sel = 0; sel < NUM_MENU; sel++)  //loop to preselect the first enabled menu entry
		if (enable[sel] == TRUE)
			break;  //break loop if sel is at an enabled menu entry

	event = 1;  //event = initial entry
	while (1) {
		//Pad response section
		waitPadReady(0, 0);
		if (readpad()) {
			if (new_pad & PAD_UP && sel < NUM_MENU) {
				event |= 2;  //event |= valid pad command
				do {
					sel--;
					if (sel < 0)
						sel = NUM_MENU - 1;
				} while (!enable[sel]);
			} else if (new_pad & PAD_DOWN && sel < NUM_MENU) {
				event |= 2;  //event |= valid pad command
				do {
					sel++;
					if (sel == NUM_MENU)
						sel = 0;
				} while (!enable[sel]);
			} else if ((new_pad & PAD_TRIANGLE) || (!swapKeys && new_pad & PAD_CROSS) || (swapKeys && new_pad & PAD_CIRCLE)) {
				return -1;
			} else if ((swapKeys && new_pad & PAD_CROSS) || (!swapKeys && new_pad & PAD_CIRCLE)) {
				event |= 2;  //event |= valid pad command
				break;
			} else if (new_pad & PAD_SQUARE && sel == PASTE) {
				event |= 2;  //event |= valid pad command
				break;
			}
		}

		if (event || post_event) {  //NB: We need to update two frame buffers per event

			//Display section
			drawPopSprite(setting->color[COLOR_BACKGR],
			              mSprite_X1, mSprite_Y1,
			              mSprite_X2, mSprite_Y2);
			drawFrame(mSprite_X1, mSprite_Y1, mSprite_X2, mSprite_Y2, setting->color[COLOR_FRAME]);

			for (i = 0, y = mSprite_Y1 + FONT_HEIGHT / 2; i < NUM_MENU; i++) {
				if (i == COPY)
					strcpy(tmp, LNG(Copy));
				else if (i == CUT)
					strcpy(tmp, LNG(Cut));
				else if (i == PASTE)
					strcpy(tmp, LNG(Paste));
				else if (i == PSUPASTE)
					strcpy(tmp, psu_action_label);
				else if (i == DELETE)
					strcpy(tmp, LNG(Delete));
				else if (i == RENAME)
					strcpy(tmp, LNG(Rename));
				else if (i == NEWDIR)
					strcpy(tmp, LNG(New_Dir));
				else if (i == NEWICON)
					strcpy(tmp, LNG(New_Icon));
				else if (i == MOUNTVMC0)
					sprintf(tmp, "%s vmc0:", LNG(Mount));
				else if (i == MOUNTVMC1)
					sprintf(tmp, "%s vmc1:", LNG(Mount));
				else if (i == GETSIZE)
					strcpy(tmp, LNG(Get_Size));
				else if (i == OPEN_TEXTEDITOR)
					strcpy(tmp, LNG(TextEditor));
				else if (i == LAUNCH_ELF_ARGS)
					strcpy(tmp, LNG(Launch_With_Args));
				else if (i == TITLE_CFG)
					strcpy(tmp, LNG(title_cfg));
#ifdef TMANIP
				else if (i == TIMEMANIP)
					strcpy(tmp, LNG(time_manip));
#endif //TMANIP

				if (enable[i])
					color = setting->color[COLOR_TEXT];
				else
					color = setting->color[COLOR_FRAME];

				printXY(tmp, mSprite_X1 + 2 * FONT_WIDTH, y, color, TRUE, 0);
				y += FONT_HEIGHT;
			}
			if (sel < NUM_MENU)
				drawChar(LEFT_CUR, mSprite_X1 + FONT_WIDTH, mSprite_Y1 + (FONT_HEIGHT / 2 + sel * FONT_HEIGHT), setting->color[COLOR_TEXT]);

			//Tooltip section
			x = SCREEN_MARGIN;
			y = Menu_tooltip_y;
			drawSprite(setting->color[COLOR_BACKGR],
			           0, y - 1,
			           SCREEN_WIDTH, y + FONT_HEIGHT);
			if (swapKeys)
				sprintf(tmp, "\xFF"
				             "1:%s \xFF"
				             "0:%s",
				        LNG(OK), LNG(Cancel));
			else
				sprintf(tmp, "\xFF"
				             "0:%s \xFF"
				             "1:%s",
				        LNG(OK), LNG(Cancel));
			if (sel == PASTE)
				sprintf(tmp + strlen(tmp), " \xFF"
				                           "2:%s",
				        LNG(PasteRename));
			sprintf(tmp + strlen(tmp), " \xFF"
			                           "3:%s",
			        LNG(Back));
			printXY(tmp, x, y, setting->color[COLOR_SELECT], TRUE, 0);
		}  //ends if(event||post_event)
		drawScr();
		post_event = event;
		event = 0;
	}  //ends while
	return sel;
}  //ends menu

char *PathPad_menu(const char *path)
{
	u64 color;
	int x, y, dx, dy, dw, dh;
	int a = 6, b = 4, c = 2, tw, th;
	int i, sel_x, sel_y;
	int event, post_event = 0;
	char textrow[80], tmp[64];

	th = 10 * FONT_HEIGHT;          //Height in pixels of text area
	tw = 68 * FONT_WIDTH;           //Width in pixels of max text row
	dh = th + 2 * 2 + a + b + c;    //Height in pixels of entire frame
	dw = tw + 2 * 2 + a * 2;        //Width in pixels of entire frame
	dx = (SCREEN_WIDTH - dw) / 2;   //X position of frame (centred)
	dy = (SCREEN_HEIGHT - dh) / 2;  //Y position of frame (centred)

	sel_x = 0;
	sel_y = 0;
	event = 1;  //event = initial entry
	while (1) {
		//Pad response section
		waitPadReady(0, 0);
		if (readpad()) {
			if (new_pad) {
				event |= 2;  //event |= valid pad command
				if (new_pad & PAD_UP) {
					sel_y--;
					if (sel_y < 0)
						sel_y = 9;
				} else if (new_pad & PAD_DOWN) {
					sel_y++;
					if (sel_y > 9)
						sel_y = 0;
				} else if (new_pad & PAD_LEFT) {
					sel_y -= 5;
					if (sel_y < 0)
						sel_y = 0;
				} else if (new_pad & PAD_RIGHT) {
					sel_y += 5;
					if (sel_y > 9)
						sel_y = 9;
				} else if (new_pad & PAD_L1) {
					sel_x--;
					if (sel_x < 0)
						sel_x = 2;
				} else if (new_pad & PAD_R1) {
					sel_x++;
					if (sel_x > 2)
						sel_x = 0;
				} else if (new_pad & PAD_TRIANGLE) {  //Pushed 'Back'
					return NULL;
				} else if (!setting->PathPad_Lock                                                            //if PathPad changes allowed ?
				           && ((!swapKeys && new_pad & PAD_CROSS) || (swapKeys && new_pad & PAD_CIRCLE))) {  //Pushed 'Clear'
					PathPad[sel_x * 10 + sel_y][0] = '\0';
				} else if ((swapKeys && new_pad & PAD_CROSS) || (!swapKeys && new_pad & PAD_CIRCLE)) {  //Pushed 'Use'
					return PathPad[sel_x * 10 + sel_y];
				} else if (!setting->PathPad_Lock && (new_pad & PAD_SQUARE)) {  //Pushed 'Set'
					strncpy(PathPad[sel_x * 10 + sel_y], path, MAX_PATH - 1);
					PathPad[sel_x * 10 + sel_y][MAX_PATH - 1] = '\0';
				}
			}  //ends 'if(new_pad)'
		}      //ends 'if(readpad())'

		if (event || post_event) {  //NB: We need to update two frame buffers per event

			//Display section
			drawSprite(setting->color[COLOR_BACKGR],
			           0, (Menu_message_y - 1),
			           SCREEN_WIDTH, (Frame_start_y));
			drawPopSprite(setting->color[COLOR_BACKGR],
			              dx, dy,
			              dx + dw, (dy + dh));
			drawFrame(dx, dy, dx + dw, (dy + dh), setting->color[COLOR_FRAME]);
			for (i = 0; i < 10; i++) {
				if (i == sel_y)
					color = setting->color[COLOR_SELECT];
				else
					color = setting->color[COLOR_TEXT];
				sprintf(textrow, "%02d=", (sel_x * 10 + i));
				strncat(textrow, PathPad[sel_x * 10 + i], 64);
				textrow[67] = '\0';
				printXY(textrow, dx + 2 + a, (dy + a + 2 + i * FONT_HEIGHT), color, TRUE, 0);
			}

			//Tooltip section
			x = SCREEN_MARGIN;
			y = Menu_tooltip_y;
			drawSprite(setting->color[COLOR_BACKGR], 0, y - 1, SCREEN_WIDTH, y + FONT_HEIGHT);

			if (swapKeys) {
				sprintf(textrow, "\xFF"
				                 "1:%s ",
				        LNG(Use));
				if (!setting->PathPad_Lock) {
					sprintf(tmp, "\xFF"
					             "0:%s \xFF"
					             "2:%s ",
					        LNG(Clear), LNG(Set));
					strcat(textrow, tmp);
				}
			} else {
				sprintf(textrow, "\xFF"
				                 "0:%s ",
				        LNG(Use));
				if (!setting->PathPad_Lock) {
					sprintf(tmp, "\xFF"
					             "1:%s \xFF"
					             "2:%s ",
					        LNG(Clear), LNG(Set));
					strcat(textrow, tmp);
				}
			}
			sprintf(tmp, "\xFF"
			             "3:%s L1/R1:%s",
			        LNG(Back), LNG(Page_leftright));
			strcat(textrow, tmp);
			printXY(textrow, x, y, setting->color[COLOR_SELECT], TRUE, 0);
		}  //ends if(event||post_event)
		drawScr();
		post_event = event;
		event = 0;
	}  //ends while
}
//------------------------------
//endfunc PathPad_menu

static int BrowserModePopup(void)
{
	char tmp[80];
	int y, i, test;
	int event, post_event = 0;

	int entry_file_show = file_show;
	int entry_file_sort = file_sort;

	int Show_len = strlen(LNG(Show_Content_as)) + 1;
	int Sort_len = strlen(LNG(Sort_Content_by)) + 1;

	int menu_len = Show_len;
	if (menu_len < (i = Sort_len))
		menu_len = i;
	if (menu_len < (i = strlen(LNG(Filename)) + strlen(LNG(_plus_Details))))
		menu_len = i;
	if (menu_len < (i = strlen(LNG(Game_Title)) + strlen(LNG(_plus_Details))))
		menu_len = i;
	if (menu_len < (i = strlen(LNG(No_Sort))))
		menu_len = i;
	if (menu_len < (i = strlen(LNG(Timestamp))))
		menu_len = i;
	if (menu_len < (i = strlen(LNG(Back_to_Browser))))
		menu_len = i;
	menu_len += 3;  //All of the above strings are indented 3 spaces, for tooltips

	int menu_ch_w = menu_len + 1;  //Total characters in longest menu string
	int menu_ch_h = 14;            //Total number of menu lines
	int mSprite_w = (menu_ch_w + 3) * FONT_WIDTH;
	int mSprite_h = (menu_ch_h + 1) * FONT_HEIGHT;
	int mSprite_X1 = SCREEN_WIDTH / 2 - mSprite_w / 2;
	int mSprite_Y1 = SCREEN_HEIGHT / 2 - mSprite_h / 2;
	int mSprite_X2 = mSprite_X1 + mSprite_w;
	int mSprite_Y2 = mSprite_Y1 + mSprite_h;

	char minuses_s[81];

	for (i = 0; i < 80; i++)
		minuses_s[i] = '-';
	minuses_s[80] = '\0';

	event = 1;  //event = initial entry
	while (1) {
		//Pad response section
		waitPadReady(0, 0);
		if (readpad()) {
			switch (new_pad) {
				case PAD_RIGHT:
					file_sort = 0;
					event |= 2;  //event |= valid pad command
					break;
				case PAD_DOWN:
					file_sort = 1;
					event |= 2;  //event |= valid pad command
					break;
				case PAD_LEFT:
					file_sort = 2;
					event |= 2;  //event |= valid pad command
					break;
				case PAD_UP:
					file_sort = 3;
					event |= 2;  //event |= valid pad command
					break;
				case PAD_CIRCLE:
					file_show = 0;
					event |= 2;  //event |= valid pad command
					break;
				case PAD_CROSS:
					file_show = 1;
					event |= 2;  //event |= valid pad command
					break;
				case PAD_SQUARE:
					file_show = 2;
					event |= 2;  //event |= valid pad command
					if ((file_show == 2) && (elisaFnt == NULL) && (elisa_failed == FALSE)) {
						int fd, res;
						elisa_failed = TRUE;  //Default to FAILED. If it succeeds, then this status will be cleared.

						res = genFixPath("uLE:/ELISA100.FNT", tmp);
						if (!strncmp(tmp, "cdrom", 5))
							strcat(tmp, ";1");
						if (res >= 0) {
							fd = genOpen(tmp, FIO_O_RDONLY);
							if (fd >= 0) {
								test = genLseek(fd, 0, SEEK_END);
								if (test == 55016) {
									elisaFnt = (unsigned char *)memalign(64, test);
									genLseek(fd, 0, SEEK_SET);
									genRead(fd, elisaFnt, test);

									elisa_failed = FALSE;
								}
								genClose(fd);
							}
						}
					}
					break;
				case PAD_TRIANGLE:
					return (file_show != entry_file_show) || (file_sort != entry_file_sort);
			}  //ends switch(new_pad)
		}      //ends if(readpad())

		if (event || post_event) {  //NB: We need to update two frame buffers per event

			//Display section
			drawPopSprite(setting->color[COLOR_BACKGR],
			              mSprite_X1, mSprite_Y1,
			              mSprite_X2, mSprite_Y2);
			drawFrame(mSprite_X1, mSprite_Y1, mSprite_X2, mSprite_Y2, setting->color[COLOR_FRAME]);

			for (i = 0, y = mSprite_Y1 + FONT_HEIGHT / 2; i < menu_ch_h; i++) {
				if (i == 0)
					sprintf(tmp, "   %s:", LNG(Show_Content_as));
				else if (i == 1)
					sprintf(tmp, "   %s", &minuses_s[80 - Show_len]);
				else if (i == 2)
					sprintf(tmp, "\xFF"
					             "0 %s",
					        LNG(Filename));
				else if (i == 3)
					sprintf(tmp, "\xFF"
					             "1 %s%s",
					        LNG(Filename), LNG(_plus_Details));
				else if (i == 4)
					sprintf(tmp, "\xFF"
					             "2 %s%s",
					        LNG(Game_Title), LNG(_plus_Details));
				else if (i == 6)
					sprintf(tmp, "   %s:", LNG(Sort_Content_by));
				else if (i == 7)
					sprintf(tmp, "   %s", &minuses_s[80 - Sort_len]);
				else if (i == 8)
					sprintf(tmp, "\xFF"
					             ": %s",
					        LNG(No_Sort));
				else if (i == 9)
					sprintf(tmp, "\xFF"
					             "; %s",
					        LNG(Filename));
				else if (i == 10)
					sprintf(tmp, "\xFF"
					             "< %s",
					        LNG(Game_Title));
				else if (i == 11)
					sprintf(tmp, "\xFF"
					             "= %s",
					        LNG(Timestamp));
				else if (i == 13)
					sprintf(tmp, "\xFF"
					             "3 %s",
					        LNG(Back_to_Browser));
				else
					tmp[0] = 0;

				printXY(tmp, mSprite_X1 + 2 * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
				//Display marker for current modes
				if ((file_show == i - 2) || (file_sort == i - 8))
					drawChar(LEFT_CUR, mSprite_X1 + FONT_WIDTH / 2, y, setting->color[COLOR_SELECT]);
				y += FONT_HEIGHT;

			}  //ends for loop handling one text row per loop

			//Tooltip section
			// x = SCREEN_MARGIN;
			y = Menu_tooltip_y;
			drawSprite(setting->color[COLOR_BACKGR],
			           0, y - 1,
			           SCREEN_WIDTH, y + FONT_HEIGHT);
		}  //ends if(event||post_event)
		drawScr();
		post_event = event;
		event = 0;
	}  //ends while
}
//------------------------------
//endfunc BrowserModePopup

// get_FilePath is the main browser function.
// It also contains the menu handler for the R1 submenu
// The static variables declared here are only for the use of
// this function and the submenu functions that it calls
//--------------------------------------------------------------
// sincro: ADD USBD_IRX_CNF mode for found IRX file for USBD.IRX
// example: getFilePath(setting->usbd_file, USBD_IRX_CNF);
// getFilePath selects a path according to the requested configuration mode.
// dlanor: ADD USBKBD_IRX_CNF mode for found IRX file for USBKBD.IRX
// example: getFilePath(setting->usbkbd_file, USBKBD_IRX_CNF);
// dlanor: ADD USBMASS_IRX_CNF mode for found IRX file for usb_mass
// example: getFilePath(setting->usbmass_file, USBMASS_IRX_CNF);
// dlanor: ADD SAVE_CNF mode returning either pure path or pathname
// dlanor: ADD return value 0=pure path, 1=pathname, negative=error/no_selection
static int browser_cd, browser_up, browser_repos, browser_pushed;
static int browser_sel, browser_nfiles;
static void submenu_func_GetSize(char *mess, char *path, FILEINFO *files);
static void submenu_func_Paste(char *mess, char *path);
static void submenu_func_psuPaste(char *mess, char *path);

static int isRootSpacerEntry(const char *path, const FILEINFO *file)
{
	return path[0] == '\0' && file->name[0] == '\0';
}

static void skipRootSpacerSelection(const char *path, FILEINFO *files, int nfiles, int *sel, int direction)
{
	int original;

	if (nfiles <= 0)
		return;

	if (direction == 0)
		direction = 1;

	original = *sel;
	while (isRootSpacerEntry(path, &files[*sel])) {
		*sel += direction;
		if (*sel >= nfiles)
			*sel = 0;
		else if (*sel < 0)
			*sel = nfiles - 1;
		if (*sel == original)
			break;
	}
}

//------ 记忆卡存档中文标题对照表(L1"标题+详细信息"显示模式) ------
//存档目录名通常为"B+区码+光盘ID"格式(如美版 BASLUS-20946xxx = BA + SLUS-20946,
//欧版 BESLES-52541xxx、日版 BISLPM-65401xxx 等)。查找流程:先从目录名中提取
//形如"4字母-数字"的光盘产品代码,再在按代码ASCII序排列的对照表中二分查找,
//命中即显示对应中文标题,未命中则回退显示存档自带的日文/英文标题。
//表内容(含日/美/欧各区ID,共7千余条)由 make_game_titles_cn.ps1 从游戏ID数据库生成,勿手改。
static const struct cnTitleEntry {
	const char *code;   //游戏光盘产品代码,如 "SLUS-20946"(10字符定长)
	const char *title;  //UTF-8 简体中文标题
} cnTitleTable[] = {
#include "game_titles_cn.h"
};

#define CN_TABLE_COUNT (sizeof(cnTitleTable) / sizeof(cnTitleTable[0]))

//尝试从 s[pos] 起提取光盘产品代码:4位字母数字(至少1个字母,兼容 CF00 等特殊前缀)
//+ 分隔符('-'/'_'/'.') + 1~5位数字(数字组间允许'.'/'_',如 SLUS_209.46 → SLUS-20946)。
//成功时把规范形式(如 "SLUS-20946")写入 code 并返回消耗的字符数;失败返回0。
//除存档目录名(如 BASLUS-20946xxx)外,也兼容 ISO 文件名(SLUS_209.46.iso)等写法。
static int extractProductCode(const char *s, int pos, char *code)
{
	int j, k, nd, letters = 0;
	char c;

	for (j = 0; j < 4; j++) {  //4位前缀:字母数字混合
		c = s[pos + j];
		if (c >= 'a' && c <= 'z') {
			c -= 'a' - 'A';
			letters++;
		} else if (c >= 'A' && c <= 'Z')
			letters++;
		else if (c < '0' || c > '9')
			return 0;
		code[j] = c;
	}
	if (letters == 0)
		return 0;  //纯数字前缀(日期/版本号等)不是产品代码
	c = s[pos + 4];
	if (c != '-' && c != '_' && c != '.')
		return 0;  //前缀后必须紧跟分隔符
	code[4] = '-';
	for (nd = 0, k = 5; nd < 5; k++) {
		c = s[pos + k];
		if (c >= '0' && c <= '9') {
			code[5 + nd] = c;
			nd++;
		} else if ((c == '.' || c == '_') && nd > 0 && s[pos + k + 1] >= '0' && s[pos + k + 1] <= '9')
			continue;  //跳过数字组间的分隔符(如 209.46)
		else
			break;
	}
	if (nd == 0)
		return 0;
	code[5 + nd] = '\0';
	return k;
}

//------ 用户自定义翻译文件 GAMETITLES.TXT ------
//自动生成与读取都跟随当前浏览的记忆卡:浏览mc0就用mc0:/SYS-CONF/下的文件,
//浏览mc1就用mc1:/SYS-CONF/下的文件,与从哪里启动无关;两张卡都没有时才回退
//到ELF所在目录。格式为每行"产品代码=简体中文标题"(#或;开头为注释行),
//GBK/UTF-8/UTF-16编码均可。用户译名优先于内置对照表,可补充未收录的游戏或
//修正内置译名。首次使用时自动加载,换卡浏览时按该卡重新加载(同一张卡上
//修改后需重启程序生效)。
#define USER_TITLE_MAX       2048            //最多自定义条目数
#define USER_TITLE_POOL_SZ   (100 * 1024)    //译名存储池
#define USER_TITLE_TEXT_MAX  96              //单条译名最大字节数(48个汉字)
#define USER_TITLE_FILE_MAX  (200 * 1024)    //文件读取上限
#define USER_TITLE_FILE      "GAMETITLES.TXT"

static struct cnTitleEntry userTitleTable[USER_TITLE_MAX];
static int userTitleCount = 0;
static int userTitleState = 0;       //0=未加载,1=已尝试(无论成败)
static int userTitleLoadedPort = -2; //已加载来源:0/1=卡槽,-1=非mc场景回退加载
static char userTitlePool[USER_TITLE_POOL_SZ];
static int userTitlePoolUsed = 0;

static char *userTitleAlloc(int n)
{
	char *p;

	if (userTitlePoolUsed + n > USER_TITLE_POOL_SZ)
		return NULL;
	p = userTitlePool + userTitlePoolUsed;
	userTitlePoolUsed += n;
	return p;
}

static int userTitleCmp(const void *a, const void *b)
{
	return strcmp(((const struct cnTitleEntry *)a)->code,
	              ((const struct cnTitleEntry *)b)->code);
}

static char *cnTrim(char *s)
{
	char *e;

	while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
		s++;
	e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
		*--e = '\0';
	return s;
}

//把带BOM的UTF-16文本转为UTF-8(仅BMP,代理区以'?'占位);返回输出长度
static int utf16TextToUtf8(char *out, const unsigned char *src, int len)
{
	int be = (src[0] == 0xFE && src[1] == 0xFF);
	int i = 2, di = 0;  //跳过BOM

	while (i + 1 < len) {
		unsigned int u = be ? ((unsigned int)src[i] << 8) | src[i + 1]
		                    : ((unsigned int)src[i + 1] << 8) | src[i];

		i += 2;
		if (u == 0)
			break;
		if (u >= 0xD800 && u <= 0xDFFF)
			u = '?';
		if (u < 0x80) {
			out[di++] = (char)u;
		} else if (u < 0x800) {
			out[di++] = (char)(0xC0 | (u >> 6));
			out[di++] = (char)(0x80 | (u & 0x3F));
		} else {
			out[di++] = (char)(0xE0 | (u >> 12));
			out[di++] = (char)(0x80 | ((u >> 6) & 0x3F));
			out[di++] = (char)(0x80 | (u & 0x3F));
		}
	}
	out[di] = '\0';
	return di;
}

//加载用户自定义翻译表;mc_port为当前浏览的记忆卡槽位(0/1),-1表示非mc浏览场景。
//换卡浏览时以浏览卡为最优先重新加载,保证译名与该卡上的文件一致。
static void loadUserTitles(int mc_port)
{
	char path[MAX_PATH];
	char line[256], code[11], conv[512];
	char *text, *ln, *eq, *title, *freebuf, *cp, *tp;
	unsigned char *filebuf;
	int fd, len = 0, flen, pos, linelen, i, adv = 0, tl, cl, found, w;
	size_t dir_len;
	int port_ix, ports_to_try[2];

	userTitleState = 1;  //已尝试(无论成败)
	userTitleCount = 0;
	userTitlePoolUsed = 0;  //重新加载时从存储池头部重新分配
	userTitleLoadedPort = mc_port;

	filebuf = malloc(USER_TITLE_FILE_MAX + 1);
	if (filebuf == NULL)
		return;

	//搜索顺序与自动生成位置一致(不管从哪里启动):
	//当前浏览的记忆卡(若已知) → 另一张卡 → ELF所在目录(最后回退)
	if (mc_port >= 0 && mc_port <= 1) {
		ports_to_try[0] = mc_port;
		ports_to_try[1] = mc_port ^ 1;
	} else {
		ports_to_try[0] = 0;
		ports_to_try[1] = 1;
	}
	for (port_ix = 0; port_ix < 2 && len <= 0; port_ix++) {
		snprintf(path, sizeof(path), "mc%d:/SYS-CONF/%s", ports_to_try[port_ix], USER_TITLE_FILE);
		fd = genOpen(path, FIO_O_RDONLY);
		if (fd >= 0) {
			len = genRead(fd, filebuf, USER_TITLE_FILE_MAX);
			genClose(fd);
		}
	}
	if (len <= 0) {
		dir_len = strnlen(LaunchElfDir, sizeof(path));
		if (dir_len < sizeof(path) && (dir_len + sizeof(USER_TITLE_FILE)) <= sizeof(path)) {
			memcpy(path, LaunchElfDir, dir_len);
			memcpy(path + dir_len, USER_TITLE_FILE, sizeof(USER_TITLE_FILE));
			fd = genOpen(path, FIO_O_RDONLY);
			if (fd >= 0) {
				len = genRead(fd, filebuf, USER_TITLE_FILE_MAX);
				genClose(fd);
			}
		}
	}
	if (len <= 0) {
		free(filebuf);
		return;  //未找到自定义文件,只用内置表
	}
	filebuf[len] = '\0';
	flen = len;
	freebuf = text = (char *)filebuf;

	//编码处理:UTF-16(带BOM)→UTF-8;UTF-8 BOM→跳过;
	//其余(GBK 或 UTF-8)在下面逐行由 raw_gbk_to_utf8 自动判别转换
	if (flen >= 2 && ((filebuf[0] == 0xFF && filebuf[1] == 0xFE) || (filebuf[0] == 0xFE && filebuf[1] == 0xFF))) {
		char *u8 = malloc(flen * 3 / 2 + 8);

		if (u8 != NULL) {
			flen = utf16TextToUtf8(u8, filebuf, flen);
			free(filebuf);
			freebuf = text = u8;
		} else {  //内存不足:跳过BOM按原样解析(内容无法命中,无害)
			text += 2;
			flen -= 2;
		}
	} else if (flen >= 3 && filebuf[0] == 0xEF && filebuf[1] == 0xBB && filebuf[2] == 0xBF) {
		text += 3;  //跳过UTF-8 BOM
		flen -= 3;
	}

	//逐行解析:"产品代码=中文标题"
	pos = 0;
	while (pos < flen && userTitleCount < USER_TITLE_MAX) {
		linelen = 0;
		while (pos < flen && text[pos] != '\n' && linelen < (int)sizeof(line) - 1)
			line[linelen++] = text[pos++];
		line[linelen] = '\0';
		if (pos < flen && text[pos] == '\n')
			pos++;

		ln = cnTrim(line);
		if (!ln[0] || ln[0] == '#' || ln[0] == ';')
			continue;  //空行与注释行
		if (ln[0] == '/' && ln[1] == '/')
			continue;
		eq = strchr(ln, '=');
		if (eq == NULL)
			continue;  //忽略无法解析的行
		*eq = '\0';
		title = cnTrim(eq + 1);
		ln = cnTrim(ln);
		//译名可留空(待补翻):空条目也入表,防止自动生成时被当成新代码重复追加

		//'='左侧提取产品代码(支持 SLUS-20946 / SLUS_209.46 / BASLUS-20946 等写法)
		found = 0;
		for (i = 0; ln[i] != '\0'; i += (adv > 0 ? adv : 1)) {
			adv = extractProductCode(ln, i, code);
			if (adv > 0) {
				found = 1;
				break;
			}
		}
		if (!found)
			continue;

		//译名编码自动判别:GBK→UTF-8(返回1);已是UTF-8/ASCII则原样使用
		if (raw_gbk_to_utf8(conv, title))
			title = conv;
		tl = strlen(title);
		if (tl > USER_TITLE_TEXT_MAX) {  //超长截断(退到UTF-8字符边界)
			tl = USER_TITLE_TEXT_MAX;
			while (tl > 0 && ((unsigned char)title[tl] & 0xC0) == 0x80)
				tl--;  //退过被截断的续字节
			if (tl > 0 && (unsigned char)title[tl - 1] >= 0xC0)
				tl--;  //被截断的多字节首字节也退掉
		}

		cl = strlen(code);
		cp = userTitleAlloc(cl + 1);
		tp = userTitleAlloc(tl + 1);
		if (cp == NULL || tp == NULL)
			break;  //存储池满,停止解析
		memcpy(cp, code, cl + 1);
		memcpy(tp, title, tl);
		tp[tl] = '\0';
		userTitleTable[userTitleCount].code = cp;
		userTitleTable[userTitleCount].title = tp;
		userTitleCount++;
	}
	free(freebuf);

	//按代码ASCII序排序 + 相邻去重(同一代码请只写一行)
	if (userTitleCount > 1) {
		qsort(userTitleTable, userTitleCount, sizeof(struct cnTitleEntry), userTitleCmp);
		w = 1;
		for (i = 1; i < userTitleCount; i++) {
			if (strcmp(userTitleTable[w - 1].code, userTitleTable[i].code) != 0)
				userTitleTable[w++] = userTitleTable[i];
		}
		userTitleCount = w;
	}
}

//在已加载的用户表中二分查找(表按代码ASCII序排列);未命中返回NULL
//(注:命中但译名为空串时返回空串指针,表示"该代码已在文件里")
static const char *userTitleLookup(const char *code)
{
	unsigned int lo = 0, hi;

	if (userTitleCount == 0)
		return NULL;
	hi = userTitleCount - 1;
	while ((int)lo <= (int)hi) {
		unsigned int mid = (lo + hi) / 2;
		int c = strcmp(userTitleTable[mid].code, code);

		if (c == 0)
			return userTitleTable[mid].title;
		if (c < 0)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return NULL;
}

//在内置对照表中二分查找(表按代码ASCII序排列);未命中返回NULL
static const char *builtinTitleLookup(const char *code)
{
	int lo = 0, hi = CN_TABLE_COUNT - 1;

	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		int c = strcmp(cnTitleTable[mid].code, code);

		if (c == 0)
			return cnTitleTable[mid].title;
		if (c < 0)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return NULL;
}

static int gtCodeCmp(const void *a, const void *b)
{
	return strcmp((const char *)a, (const char *)b);
}

//进入L1"标题+详细信息"模式浏览记忆卡目录时,自动生成/更新当前浏览记忆卡上的
//GAMETITLES.TXT(浏览mc0→mc0:/SYS-CONF/,浏览mc1→mc1:/SYS-CONF/,与启动位置无关):
//扫描当前目录全部存档名,把其中的产品代码追加到文件——内置对照表有译名的自动
//填上,没有的等号后留空留给用户补翻。文件里已有的行(含用户填写的译名与注释)
//原样保留、永不覆盖;没有新增代码时不写盘(避免记忆卡无谓写入)。
static void syncGametitlesFile(const char *path, const FILEINFO *files, int nfiles)
{
	static char lastPath[MAX_PATH] = "";
	static int lastCount = -1;
	char dest[MAX_PATH], code[11];
	char (*codes)[11];
	char *old = NULL, *out, *op;
	int fd, len, oldLen = 0, nCodes = 0, nNew, i, j, adv;
	int mc_port, isNew = 1;
	const char *t;

	//防重入:同一目录且条目数未变 → 本次会话不再重复处理
	if (nfiles <= 0 || (!strcmp(lastPath, path) && lastCount == nfiles))
		return;
	strcpy(lastPath, path);
	lastCount = nfiles;

	//当前浏览的记忆卡槽位("mc0:/..."→0,"mc1:/..."→1)
	mc_port = (path[2] >= '0' && path[2] <= '1') ? path[2] - '0' : 0;

	//确保用户表已加载且来自当前浏览的卡(换卡后重新加载,新代码的判定才与该卡文件一致)
	if (!userTitleState || userTitleLoadedPort != mc_port)
		loadUserTitles(mc_port);

	//1. 扫描当前目录,提取所有产品代码(目录内去重)
	codes = malloc(sizeof(code) * nfiles);
	if (codes == NULL)
		return;
	for (i = 0; i < nfiles; i++) {
		adv = 0;
		for (j = 0; files[i].name[j] != '\0'; j += (adv > 0 ? adv : 1)) {
			adv = extractProductCode(files[i].name, j, code);
			if (adv > 0)
				break;
		}
		if (adv <= 0)
			continue;
		for (j = 0; j < nCodes; j++)
			if (!strcmp(codes[j], code))
				break;
		if (j == nCodes)
			strcpy(codes[nCodes++], code);
	}
	if (nCodes == 0) {
		free(codes);
		return;  //本目录没有带产品代码的存档
	}

	//2. 目标固定为当前浏览的记忆卡:浏览mc0就生成在mc0,浏览mc1就生成在mc1,
	//   不管从哪里启动;SYS-CONF目录不存在则先创建(已存在时失败无害)
	snprintf(dest, sizeof(dest), "mc%d:/SYS-CONF/%s", mc_port, USER_TITLE_FILE);
	fd = genOpen(dest, FIO_O_RDONLY);
	if (fd < 0)
		mcMkDir(mc_port, 0, "SYS-CONF");

	//3. 读现有文件内容(存在则),原样保留
	if (fd >= 0) {
		old = malloc(USER_TITLE_FILE_MAX + 1);
		if (old == NULL) {  //内存不足:放弃,不动用户文件
			genClose(fd);
			free(codes);
			return;
		}
		len = genRead(fd, old, USER_TITLE_FILE_MAX);
		genClose(fd);
		if (len <= 0) {
			//文件存在但读取失败:为防覆盖用户数据,放弃本次同步
			free(old);
			free(codes);
			return;
		}
		oldLen = len;
		isNew = 0;
	}

	//4. 过滤出文件中尚不存在的代码(空译名条目也算已存在)
	nNew = 0;
	for (i = 0; i < nCodes; i++) {
		if (userTitleLookup(codes[i]) == NULL)
			memcpy(codes[nNew++], codes[i], sizeof(code));
	}
	if (nNew == 0) {
		free(codes);
		free(old);
		return;  //没有新增代码,不写盘
	}
	qsort(codes, nNew, sizeof(code), gtCodeCmp);

	//5. 拼接输出:新文件写说明头;已有内容原样保留 + 追加新条目
	out = malloc(oldLen + nNew * 160 + 1024);
	if (out == NULL) {
		free(codes);
		free(old);
		return;
	}
	op = out;
	if (isNew) {
		op += sprintf(op, "# GAMETITLES.TXT —— 自定义游戏中文标题(本文件由程序自动生成)\n");
		op += sprintf(op, "# 格式: 产品代码=简体中文标题;等号后留空表示待补翻\n");
		op += sprintf(op, "# '#'或';'开头为注释行;修改保存后需重新运行程序生效\n");
	} else {
		memcpy(op, old, oldLen);
		op += oldLen;
		if (oldLen > 0 && op[-1] != '\n')
			*op++ = '\n';  //确保原内容以换行结尾
	}
	for (i = 0; i < nNew; i++) {
		t = builtinTitleLookup(codes[i]);
		op += sprintf(op, "%s=%s\n", codes[i], (t != NULL) ? t : "");
	}

	//6. 写盘(仅在确有新增条目时才会走到这里)
	fd = genOpen(dest, FIO_O_CREAT | FIO_O_WRONLY | FIO_O_TRUNC);
	if (fd >= 0) {
		genWrite(fd, out, (int)(op - out));
		genClose(fd);
	}
	free(out);
	free(old);
	free(codes);
}

//从存档目录/文件名中提取光盘产品代码,先查用户自定义表(GAMETITLES.TXT,可覆盖
//内置译名),再查内置对照表(均按代码ASCII序二分查找);未命中返回NULL。
//每次调用对name逐位置扫描,但二分查找仅对提取出的候选代码进行,7千余条表也只需约13次比较。
static const char *lookupCNTitle(const char *name)
{
	unsigned int i = 0;

	if (name == NULL || name[0] == '\0')
		return NULL;
	if (!userTitleState)
		loadUserTitles(-1);  //首次使用时加载(非mc浏览场景:mc0→mc1→ELF目录)
	while (name[i] != '\0') {
		char code[11];  //4字母 + '-' + 5数字 + '\0'
		int adv;

		adv = extractProductCode(name, (int)i, code);
		if (adv > 0) {
			const char *t = userTitleLookup(code);

			if (t != NULL && t[0] != '\0')
				return t;  //用户自定义译名(空译名=待补翻,继续查内置表)
			//内置表二分查找(表按代码ASCII序排列)
			t = builtinTitleLookup(code);
			if (t != NULL)
				return t;
			i += adv;  //跳过已扫描的模式继续找下一个候选
			continue;
		}
		i++;
	}
	return NULL;
}

int getFilePath(char *out, int cnfmode)
{
	char path[MAX_PATH], cursorEntry[MAX_PATH],
	    msg0[MAX_PATH], msg1[MAX_PATH],
	    tmp[MAX_PATH], tmp1[MAX_PATH], tmp2[MAX_PATH], ext[8], *p;
	const unsigned char *mcTitle;
	const char *cnTitle;  //产品代码对照表命中的中文标题(UTF-8)
	u64 color;
	FILEINFO files[MAX_ENTRY];
	int top = 0, rows;
	int x, y, y0, y1;
	int i, j, ret, rv = -1;  //NB: rv is for return value of this function
	int usb_unit;
	int event, post_event = 0;
	int font_height;
	int iconbase, iconcolr;
	int scroll_px, scroll_last_sel, scroll_active;  //长名字跑马灯状态
	u64 scroll_tick;

	elisa_failed = FALSE;  //set at failure to load font, cleared at each browser entry

	browser_cd = TRUE;
	browser_up = FALSE;
	browser_repos = FALSE;
	browser_pushed = TRUE;
	browser_sel = 0;
	browser_nfiles = 0;

	strcpy(ext, cnfmode_extL[cnfmode]);

	if ((cnfmode == USBD_IRX_CNF) || (cnfmode == USBKBD_IRX_CNF) || (cnfmode == USBMASS_IRX_CNF) || ((!strncmp(LastDir, setting->Misc, strlen(setting->Misc))) && (cnfmode > LK_ELF_CNF)))
		path[0] = '\0';  //start in main root if recent folder unreasonable
	else
		strcpy(path, LastDir);  //If reasonable, start in recent folder

	unmountAll();  //unmount all uLE-used mountpoints

	clipPath[0] = 0;
	nclipFiles = 0;
	browser_cut = 0;

	file_show = 1;
	file_sort = 1;

	font_height = FONT_HEIGHT;
	if ((file_show == 2) && (elisaFnt != NULL))
		font_height = FONT_HEIGHT + 2;
	rows = (Menu_end_y - Menu_start_y) / font_height;

	//长名字跑马灯状态：选中项名字超宽时滚动显示（支持任意长度中文名）
	scroll_px = 0;
	scroll_last_sel = -1;
	scroll_active = 0;
	scroll_tick = Timer();

	event = 1;  //event = initial entry
	while (1) {

		//Pad response section
		waitPadReady(0, 0);
		if (readpad()) {
			int step;
			if (new_pad) {
				browser_pushed = TRUE;
				event |= 2;  //event |= pad command
			}
			if (new_pad & PAD_UP) {
				if (browser_nfiles > 0) {
					if (browser_sel > 0)
						browser_sel--;
					else
						browser_sel = browser_nfiles - 1;
					skipRootSpacerSelection(path, files, browser_nfiles, &browser_sel, -1);
				}
			} else if (new_pad & PAD_DOWN) {
				if (browser_nfiles > 0) {
					if (browser_sel < browser_nfiles - 1)
						browser_sel++;
					else
						browser_sel = 0;
					skipRootSpacerSelection(path, files, browser_nfiles, &browser_sel, 1);
				}
				} else if (new_pad & PAD_LEFT) {
					if (browser_nfiles > 0) {
						step = rows / 2;
						if (step < 1)
							step = 1;
						if (browser_sel == 0)
							browser_sel = browser_nfiles - 1;
						else if (browser_sel < step)
							browser_sel = 0;
						else
							browser_sel -= step;
						skipRootSpacerSelection(path, files, browser_nfiles, &browser_sel, -1);
					}
				} else if (new_pad & PAD_RIGHT) {
					if (browser_nfiles > 0) {
						step = rows / 2;
						if (step < 1)
							step = 1;
						if (browser_sel == browser_nfiles - 1)
							browser_sel = 0;
						else if (browser_sel >= (browser_nfiles - step))
							browser_sel = browser_nfiles - 1;
						else
							browser_sel += step;
						skipRootSpacerSelection(path, files, browser_nfiles, &browser_sel, 1);
					}
				}
			else if (new_pad & PAD_TRIANGLE)
				browser_up = TRUE;
			else if ((swapKeys && (new_pad & PAD_CROSS)) || (!swapKeys && (new_pad & PAD_CIRCLE))) {  //Pushed OK
				if (files[browser_sel].stats.AttrFile & sceMcFileAttrSubdir) {
					//pushed OK for a folder
					if (!strcmp(files[browser_sel].name, ".."))
						browser_up = TRUE;
					else {
						strcat(path, files[browser_sel].name);
						strcat(path, "/");
						browser_cd = TRUE;
					}
				} else {
					//pushed OK for a file
					sprintf(out, "%s%s", path, files[browser_sel].name);
					// Must to include a function for check IRX Header
					if (((cnfmode == LK_ELF_CNF) || (cnfmode == NON_CNF)) && (!IsSupportedFileType(out))) {
						browser_pushed = FALSE;
						sprintf(msg0, "%s.", LNG(This_file_isnt_an_ELF));
						out[0] = 0;
					} else {
						strcpy(LastDir, path);
						rv = 1;  //flag pathname selected
						break;
					}
				}
			} else if (new_pad & PAD_R3) {  //New clause for uLE-relative paths
				if (files[browser_sel].stats.AttrFile & sceMcFileAttrSubdir) {
					//pushed R3 for a folder (navigate to uLE CNF folder)
					strcpy(path, LaunchElfDir);
					if ((p = strchr(path, ':'))) {                         //device separator ?
						if (p[1] != '/') {                                 //missing path separator ? (mass: & host:)
							p[1] = '/';                                    //insert path separator
							strcpy(p + 2, LaunchElfDir + (p - path) + 1);  //append rest of pathname
						}
					}
					browser_cd = TRUE;
				} else {
					//pushed R3 for a file (treat as uLE-related)
					sprintf(out, "%s%s", path, files[browser_sel].name);
					// Must to include a function for check IRX Header
					if (((cnfmode == LK_ELF_CNF) || (cnfmode == NON_CNF)) && (checkELFheader(out) < 0)) {
						browser_pushed = FALSE;
						sprintf(msg0, "%s.", LNG(This_file_isnt_an_ELF));
						out[0] = 0;
					} else {
						strcpy(LastDir, path);
						sprintf(out, "%s%s", "uLE:/", files[browser_sel].name);
						rv = 1;  //flag pathname selected
						break;
					}
				}
			} else if (new_pad & PAD_R2) {
				char *temp = PathPad_menu(path);

				if (temp != NULL) {
					strcpy(path, temp);
					browser_cd = TRUE;
					vfreeSpace = FALSE;
				}
			} else if (new_pad & PAD_L1) {
				browser_cd = BrowserModePopup();
			}
			//pad checks above are for commands common to all browser modes
			//pad checks below are for commands that differ depending on cnfmode
			if (cnfmode == DIR_CNF) {
				if (new_pad & PAD_START) {
					strcpy(out, path);
					strcpy(LastDir, path);
					rv = 0;  //flag pathname selected
					break;
				}
			} else if (cnfmode == SAVE_CNF) {  //Generic Save commands
				if (new_pad & PAD_START) {
					if (files[browser_sel].stats.AttrFile & sceMcFileAttrSubdir) {
						//no file was highlighted, so prep to save with empty filename
						strcpy(out, path);
						rv = 0;  //flag pure path selected
					} else {
						//a file was highlighted, so prep to save with that filename
						sprintf(out, "%s%s", path, files[browser_sel].name);
						rv = 1;  //flag pathname selected
					}
					strcpy(LastDir, path);
					break;
				}
			}
			if (cnfmode) {  //A file is to be selected, not in normal browser mode
				if (new_pad & PAD_SQUARE) {
					if (!strcmp(ext, "*"))
						strcpy(ext, cnfmode_extL[cnfmode]);
					else
						strcpy(ext, "*");
					browser_cd = TRUE;
				} else if ((!swapKeys && (new_pad & PAD_CROSS)) || (swapKeys && (new_pad & PAD_CIRCLE))) {  //Cancel command ?
					LaunchArgsClear();
					unmountAll();
					return rv;
				}
			} else {  //cnfmode == FALSE
				if (new_pad & PAD_R1) {
					ret = menu(path, &files[browser_sel]);
					if (ret == COPY || ret == CUT) {
						if (ret == CUT) {
							const FILEINFO *protected_file = NULL;

							if (nmarks > 0) {
								for (i = 0; i < browser_nfiles; i++) {
									if (marks[i] && filerIsExploitProtectedPath(path, &files[i])) {
										protected_file = &files[i];
										break;
									}
								}
							} else if (filerIsExploitProtectedPath(path, &files[browser_sel]))
								protected_file = &files[browser_sel];
							if (protected_file != NULL) {
								if (filerConfirmExploitModify(path, protected_file) < 0) {
									browser_pushed = FALSE;
									continue;
								}
							}
						}
						strcpy(clipPath, path);
						if (nmarks > 0) {
							for (i = nclipFiles = 0; i < browser_nfiles; i++)
								if (marks[i] && strcmp(files[i].name, ".") && strcmp(files[i].name, ".."))
									clipFiles[nclipFiles++] = files[i];
							if (nclipFiles == 0) {
								strcpy(msg0, LNG(Failed));
								browser_pushed = FALSE;
								continue;
							}
						} else {
							if (!strcmp(files[browser_sel].name, ".") || !strcmp(files[browser_sel].name, "..")) {
								strcpy(msg0, LNG(Failed));
								browser_pushed = FALSE;
								continue;
							}
							clipFiles[0] = files[browser_sel];
							nclipFiles = 1;
						}
						clipIopResetGeneration = getIopResetGeneration();
						sprintf(msg0, "%s", LNG(Copied_to_the_Clipboard));
						browser_pushed = FALSE;
						if (ret == CUT)
							browser_cut = TRUE;
						else
							browser_cut = FALSE;
					}  //ends COPY and CUT
					else if (ret == DELETE) {
						if (nmarks == 0) {  //dlanor: using title was inappropriate here (filesystem op)
							if (filerIsExploitProtectedPath(path, &files[browser_sel])) {
								ret = filerConfirmExploitDelete(path, &files[browser_sel]);
							} else {
								sprintf(tmp, "%s", files[browser_sel].name);
								if (files[browser_sel].stats.AttrFile & sceMcFileAttrSubdir)
									strcat(tmp, "/");
								sprintf(tmp1, "\n%s ?", LNG(Delete));
								strcat(tmp, tmp1);
								ret = ynDialog(tmp);
							}
						} else
							ret = ynDialog(LNG(Mark_Files_Delete));

						if (ret > 0) {
							int first_deleted = -1;
							if (nmarks == 0) {
								strcpy(tmp, files[browser_sel].name);
								if (files[browser_sel].stats.AttrFile & sceMcFileAttrSubdir)
									strcat(tmp, "/");
								sprintf(tmp1, " %s", LNG(deleting));
								strcat(tmp, tmp1);
								drawMsg(tmp);
								ret = delete (path, &files[browser_sel]);
								if (ret >= 0)
									first_deleted = browser_sel;
							} else {
								for (i = 0; i < browser_nfiles; i++) {
									if (marks[i]) {
										if (filerIsExploitProtectedPath(path, &files[i]) &&
										    filerConfirmExploitDelete(path, &files[i]) < 0)
											continue;
										if (first_deleted < 0)   //if this is the first mark
											first_deleted = i;  //then memorize it for cursor positioning
										strcpy(tmp, files[i].name);
										if (files[i].stats.AttrFile & sceMcFileAttrSubdir)
											strcat(tmp, "/");
										sprintf(tmp1, " %s", LNG(deleting));
										strcat(tmp, tmp1);
										drawMsg(tmp);
										ret = delete (path, &files[i]);
										if (ret < 0)
											break;
									}
								}
							}
							if (ret >= 0) {
								if (first_deleted >= 0) {
									int cursor_source = first_deleted - 1;
									if (cursor_source < 0)
										cursor_source = 0;
									strcpy(cursorEntry, files[cursor_source].name);
								}
							} else {
								strcpy(cursorEntry, files[browser_sel].name);
								sprintf(msg0, "%s Err=%d", LNG(Delete_Failed), ret);
								browser_pushed = FALSE;
							}
							browser_cd = TRUE;
							browser_repos = TRUE;
						}
					}  //ends DELETE
					else if (ret == RENAME) {
						if (filerIsExploitProtectedPath(path, &files[browser_sel])) {
							browser_pushed = FALSE;
							strcpy(msg0, LNG(Rename_Failed));
						} else {
							strcpy(tmp, files[browser_sel].name);
							if (keyboard(tmp, 160) > 0) {
								if (Rename(path, &files[browser_sel], tmp) < 0) {
									browser_pushed = FALSE;
									strcpy(msg0, LNG(Rename_Failed));
								} else
									browser_cd = TRUE;
							}
						}
					}  //ends RENAME
					else if (ret == PASTE) {
						if (filerConfirmExploitModify(path, NULL) > 0)
							submenu_func_Paste(msg0, path);
						else
							browser_pushed = FALSE;
					} else if (ret == PSUPASTE) {
						if (filerConfirmExploitModify(path, NULL) > 0)
							submenu_func_psuPaste(msg0, path);
						else
							browser_pushed = FALSE;
					}
					else if (ret == NEWDIR) {
						tmp[0] = 0;
						if (filerConfirmExploitModify(path, NULL) > 0 && keyboard(tmp, 160) > 0) {
							ret = newdir(path, tmp);
							if (ret == -17) {
								strcpy(msg0, LNG(directory_already_exists));
								browser_pushed = FALSE;
							} else if (ret < 0) {
								strcpy(msg0, LNG(NewDir_Failed));
								browser_pushed = FALSE;
							} else {  //dlanor: modified for similarity to PC browsers
								sprintf(msg0, "%s: ", LNG(Created_folder));
								strcat(msg0, tmp);
								browser_pushed = FALSE;
								strcpy(cursorEntry, tmp);
								browser_repos = TRUE;
								browser_cd = TRUE;
							}
						}
					}  //ends NEWDIR
					else if (ret == NEWICON) {
						if (filerConfirmExploitModify(path, NULL) < 0) {
							browser_pushed = FALSE;
							continue;
						}
						strcpy(tmp, LNG(Icon_Title));
							if (keyboard(tmp, 160) <= 0)
								goto DoneIcon;
							if (genFixPath(path, tmp1) < 0) {
								sprintf(msg0, "Path conversion failed: %s", path);
								goto DoneIcon;
							}
							strcat(tmp1, "icon.sys");
							if ((ret = genOpen(tmp1, FIO_O_RDONLY)) >= 0) {  //if old "icon.sys" file exists
								genClose(ret);
							sprintf(msg1,
							        "\n\"icon.sys\" %s.\n\n%s ?", LNG(file_alredy_exists),
							        LNG(Do_you_wish_to_overwrite_it));
							if (ynDialog(msg1) < 0)
								goto DoneIcon;
							genRemove(tmp1);
							}
							make_iconsys(tmp, "icon.icn", tmp1);
							browser_cd = TRUE;
							strcpy(tmp, LNG(IconText));
							keyboard(tmp, 160);
							if (genFixPath(path, tmp1) < 0) {
								sprintf(msg0, "Path conversion failed: %s", path);
								goto DoneIcon;
							}
							strcat(tmp1, "icon.icn");
							if ((ret = genOpen(tmp1, FIO_O_RDONLY)) >= 0) {  //if old "icon.icn" file exists
								genClose(ret);
							sprintf(msg1,
							        "\n\"icon.icn\" %s.\n\n%s ?", LNG(file_alredy_exists),
							        LNG(Do_you_wish_to_overwrite_it));
							if (ynDialog(msg1) < 0)
								goto DoneIcon;
							genRemove(tmp1);
						}
						make_icon(tmp, tmp1);
					DoneIcon:
						strcpy(tmp, tmp1);  //Dummy code to make 'goto DoneIcon' legal for gcc
					}                       //ends NEWICON
					else if ((ret == MOUNTVMC0) || (ret == MOUNTVMC1)) {
						i = ret - MOUNTVMC0;
						sprintf(tmp, "vmc%d:", i);
						if (vmcMounted[i]) {
							if ((j = vmc_PartyIndex[i]) >= 0) {
								vmc_PartyIndex[i] = -1;
								if (j != vmc_PartyIndex[1 ^ i])
									Party_vmcIndex[j] = -1;
							}
							fileXioUmount(tmp);
							vmcMounted[i] = 0;
						}
						j = genFixPath(path, tmp1);
						if (j < 0) {
							sprintf(msg1, "\n'%s vmc%d:' for \"%s\"\nResult=%d",
							        LNG(Mount), i, path, j);
							(void)ynDialog(msg1);
							browser_pushed = FALSE;
							continue;
						}
						strcpy(tmp2, tmp1);
#if defined(ETH) || defined(UDPFS)
						if (!strncmp(path, "host:", 5) || !strncmp(path, "udpfs:", 6)) {
							makeHostPath(tmp2, tmp1);
						}
#endif
						strcat(tmp2, files[browser_sel].name);
						/* genFixPath may reset the IOP while lazy-loading storage stacks. */
						x = load_vmcman();
						if (!x) {
							x = get_vmcman_last_error();
							sprintf(msg1, "\n'%s vmc%d:' for \"%s\"\nvmcman not registered\nResult=%d",
							        LNG(Mount), i, tmp2, x);
							(void)ynDialog(msg1);
						} else if ((x = fileXioMount(tmp, tmp2, FIO_MT_RDWR)) >= 0) {
							if ((j >= 0) && (j < MOUNT_LIMIT)) {
								vmc_PartyIndex[i] = j;
								Party_vmcIndex[j] = i;
							}
							vmcMounted[i] = 1;
							snprintf(path, sizeof(path), "%.*s/", (int)sizeof(path) - 2, tmp);
							browser_cd = TRUE;
							cnfmode = NON_CNF;
							strcpy(ext, cnfmode_extL[cnfmode]);
						} else {
							sprintf(msg1, "\n'%s vmc%d:' for \"%s\"\nResult=%d",
							        LNG(Mount), i, tmp2, x);
							(void)ynDialog(msg1);
						}
					}  //ends MOUNTVMCx
					else if (ret == OPEN_TEXTEDITOR) {
						if (filerConfirmExploitModify(path, &files[browser_sel]) > 0) {
							snprintf(tmp1, sizeof(tmp1), "%s%s", path, files[browser_sel].name);
							ret = TextEditor(tmp1);
							strcpy(cursorEntry, files[browser_sel].name);
							browser_pushed = FALSE;
							browser_repos = TRUE;
							browser_cd = TRUE;
							if (ret == TEXTEDITOR_RESULT_LAUNCH_ARGS)
								snprintf(msg0, sizeof(msg0), LNG(Launch_Args_Loaded), LaunchArgsGetCount());
							else
								LaunchArgsClear();
						} else
							browser_pushed = FALSE;
					}  //ends OPEN_TEXTEDITOR
					else if (ret == LAUNCH_ELF_ARGS) {
						snprintf(tmp1, sizeof(tmp1), "%s%s", path, files[browser_sel].name);
						if (LaunchArgsLoadFromFile(tmp1, msg0, sizeof(msg0)) > 0) {
							strcpy(cursorEntry, files[browser_sel].name);
							browser_repos = TRUE;
						}
						browser_pushed = FALSE;
					}  //ends LAUNCH_ELF_ARGS
					else if (ret == GETSIZE) {
						submenu_func_GetSize(msg0, path, files);
					}  //ends GETSIZE
//#ifdef TMANIP
					else if (ret == TIMEMANIP) {
#ifdef TMANIP_MORON
						sprintf(msg1, "\n\n %s  [%s]  ?\n", LNG(change_timestamp_of), HACK_FOLDER);
#else
						sprintf(msg1, "\n\n %s  [%s]  ?\n", LNG(change_timestamp_of), files[browser_sel].name);
#endif //TMANIP_MORON
						if (filerConfirmExploitModify(path, &files[browser_sel]) > 0 && ynDialog(msg1) > 0) {
							time_manip(path, &files[browser_sel], msg0);
							browser_pushed = FALSE;
							browser_repos = TRUE;  // TEST
							browser_cd = TRUE;     //TEST
						}
					}
//#endif //TMANIP

				else if (ret == TITLE_CFG) {
					if (filerConfirmExploitModify(path, &files[browser_sel]) > 0) {
						make_title_cfg(path, &files[browser_sel], msg0);
						browser_pushed = FALSE;
						browser_repos = TRUE;  // TEST
						browser_cd = TRUE;     //TEST
					} else
						browser_pushed = FALSE;
				}
				   //R1 menu handling is completed above
			} else if ((!swapKeys && new_pad & PAD_CROSS) || (swapKeys && new_pad & PAD_CIRCLE)) {
				if (browser_sel != 0 && strcmp(files[browser_sel].name, ".") && strcmp(files[browser_sel].name, "..") && path[0] != 0 && (!isHddRootPath(path) && strcmp(path, "dvr_hdd0:/"))) {
					if (marks[browser_sel]) {
						marks[browser_sel] = FALSE;
						nmarks--;
					} else {
						marks[browser_sel] = TRUE;
						nmarks++;
					}
				}
				browser_sel++;
				if (browser_sel >= browser_nfiles)
					browser_sel = 0;
				skipRootSpacerSelection(path, files, browser_nfiles, &browser_sel, 1);
			} else if (new_pad & PAD_SQUARE) {
				if (path[0] != 0 && (!isHddRootPath(path) && strcmp(path, "dvr_hdd0:/"))) {
					for (i = 1; i < browser_nfiles; i++) {
						if (marks[i]) {
							marks[i] = FALSE;
							nmarks--;
						} else {
							marks[i] = TRUE;
							nmarks++;
						}
					}
				}
			} else if (new_pad & PAD_SELECT) {  //Leaving the browser ?
				LaunchArgsClear();
				unmountAll();
				return rv;
			}
			}
		}  //ends pad response section

		//browser path adjustment section
		if (browser_up) {
			if ((p = strrchr(path, '/')) != NULL)
				*p = 0;
			if ((p = strrchr(path, '/')) != NULL) {
				p++;
				strcpy(cursorEntry, p);
				*p = 0;
			} else {
				strcpy(cursorEntry, path);
				path[0] = 0;
			}
			browser_cd = TRUE;
			browser_repos = TRUE;
		}  //ends 'if(browser_up)'
		if (!browser_cd && path[0] == '\0' && !boot_show_all_devices && setting != NULL && setting->Hide_MCMMCE && pollRootMemoryCardDevices()) {
			if (browser_nfiles > 0)
				strcpy(cursorEntry, files[browser_sel].name);
			else
				cursorEntry[0] = '\0';
			browser_cd = TRUE;
			browser_repos = TRUE;
			event |= 2;
		}
		//----- Process newly entered directory here (incl initial entry)
		if (browser_cd) {
			if (isGenericUsbRootPath(path)) {
				usb_unit = prepareUsbRootBrowse();
				if (usb_unit >= 0)
					snprintf(path, sizeof(path), "usb%d:/", usb_unit);
			}
			browser_nfiles = setFileList(path, ext, files, cnfmode);
#ifdef UDPFS
			if (udpfs_dir_open_failed) {
				udpfs_dir_open_failed = 0;
				strcpy(msg0, LNG(UDPFS_Server_not_found));
				browser_pushed = FALSE;
				rebootIopAndReloadCoreStackSilent();
				path[0] = '\0';
				strcpy(cursorEntry, "udpfs:");
				browser_repos = TRUE;
				browser_nfiles = setFileList(path, ext, files, cnfmode);
			}
#endif
			if (!cnfmode) {  //Calculate free space (unless configuring)
				if (!strncmp(path, "mc", 2)) {
					mcGetInfo(path[2] - '0', 0, &mctype_PSx, &mcfreeSpace, NULL);
					mcSync(0, NULL, &ret);
					freeSpace = mcfreeSpace * ((mctype_PSx == 1) ? 8192 : 1024);
					vfreeSpace = TRUE;
#ifdef XFROM
				} else if (!strncmp(path, "xfrom", 5)) {
					xfromGetInfo(0, 0, &mctype_PSx, &mcfreeSpace, NULL);
					xfromSync(0, NULL, &ret);
					freeSpace = mcfreeSpace * ((mctype_PSx == 1) ? 8192 : 1024);
					vfreeSpace = TRUE;
#endif
				} else if (!strncmp(path, "hdd", 3) && !isHddRootPath(path)) {
					u64 ZoneFree, ZoneSize;
					char pfs_str[6];

					strcpy(pfs_str, "pfs0:");
					pfs_str[3] += latestMount;
					ZoneFree = fileXioDevctl(pfs_str, PFSCTL_GET_ZONE_FREE, NULL, 0, NULL, 0);
					ZoneSize = fileXioDevctl(pfs_str, PFSCTL_GET_ZONE_SIZE, NULL, 0, NULL, 0);
					freeSpace = ZoneFree * ZoneSize;
					vfreeSpace = TRUE;
#ifdef DVRP
				} else if (!strncmp(path, "dvr_hdd", 7) && strcmp(path, "dvr_hdd0:/")) {
					u64 ZoneFree, ZoneSize;
					char pfs_str[10];

					strcpy(pfs_str, "dvr_pfs0:");
					pfs_str[7] += latestDVRPMount;
					ZoneFree = fileXioDevctl(pfs_str, PFSCTL_GET_ZONE_FREE, NULL, 0, NULL, 0);
					ZoneSize = fileXioDevctl(pfs_str, PFSCTL_GET_ZONE_SIZE, NULL, 0, NULL, 0);
					//printf("ZoneFree==%d  ZoneSize==%d\r\n", ZoneFree, ZoneSize);
					freeSpace = ZoneFree * ZoneSize;
					vfreeSpace = TRUE;
#endif
				}
			}
			browser_sel = 0;
			top = 0;
			if (browser_repos) {
				browser_repos = FALSE;
				for (i = 0; i < browser_nfiles; i++) {
					if (!strcmp(cursorEntry, files[i].name)) {
						browser_sel = i;
						top = browser_sel - 3;
						break;
					}
				}
			}  //ends if(browser_repos)
			nmarks = 0;
			memset(marks, 0, MAX_ENTRY);
			browser_cd = FALSE;
			browser_up = FALSE;
		}  //ends if(browser_cd)
		if (!strncmp(path, "cdfs", 4))
			uLE_cdStop();
		if (top > browser_nfiles - rows)
			top = browser_nfiles - rows;
		if (top < 0)
			top = 0;
		if (browser_sel >= browser_nfiles)
			browser_sel = browser_nfiles - 1;
		if (browser_sel < 0)
			browser_sel = 0;
		if (browser_nfiles > 0)
			skipRootSpacerSelection(path, files, browser_nfiles, &browser_sel, 1);
		if (browser_sel >= top + rows)
			top = browser_sel - rows + 1;
		if (browser_sel < top)
			top = browser_sel;

		//长名字跑马灯驱动：选中项名字超宽时每 120ms 前进一个字符位，
		//触发重绘（event|=4）；循环由 drawScr() 的 vsync 同步节流
		{
			u64 now = Timer();

			if (browser_sel != scroll_last_sel) {  //切换选中项 → 回到开头重新停留
				scroll_last_sel = browser_sel;
				scroll_px = 0;
				scroll_tick = now;
			}
			if (scroll_active && (now - scroll_tick) >= 120) {
				scroll_px += 16;
				scroll_tick = now;
				event |= 4;  //触发一次重绘
			}
		}

		//进入L1"标题+详细信息"模式浏览记忆卡时,自动生成/更新GAMETITLES.TXT:
		//把本目录存档的产品代码写入清单,内置表有译名的自动填上,没有的留空待补
		if ((file_show == 2) && (path[0] == 'm' && path[1] == 'c'))
			syncGametitlesFile(path, files, browser_nfiles);

		if (event || post_event) {  //NB: We need to update two frame buffers per event

			//Display section
			clrScr(setting->color[COLOR_BACKGR]);

			x = Menu_start_x;
			y = Menu_start_y;
			font_height = FONT_HEIGHT;
			if ((file_show == 2) && (elisaFnt != NULL)) {
				y -= 2;
				font_height = FONT_HEIGHT + 2;
			}
			rows = (Menu_end_y - Menu_start_y) / font_height;
			scroll_active = 0;  //每帧复位，由选中行超宽时置位
			for (i = 0; i < rows; i++)  //Repeat loop for each browser text row
			{
				mcTitle = NULL;      //Assume that normal file/folder names are to be displayed
				int name_limit = 0;  //Assume that no name length problems exist

				if (top + i >= browser_nfiles)
					break;
				if (isRootSpacerEntry(path, &files[top + i])) {
					y += font_height;
					continue;
				}
				if (top + i == browser_sel)
					color = setting->color[COLOR_SELECT];  //Highlight cursor line
				else
					color = setting->color[COLOR_TEXT];

				if (!strcmp(files[top + i].name, ".."))
					strcpy(tmp, "..");

				else if ((file_show == 2) && (cnTitle = lookupCNTitle(files[top + i].name)) != NULL) {
					//命中产品代码对照表:显示简体中文标题
					//(走UTF-8渲染管线,超宽时同样有跑马灯滚动)
					strcpy(tmp, cnTitle);
					name_limit = 43 * 8;
				} else if ((file_show == 2) && files[top + i].title[0] != 0) {
					mcTitle = files[top + i].title;
				} else {  //Show normal file/folder names
					const char *root_label;

					root_label = NULL;
					if (path[0] == 0)
						root_label = getRootDeviceLabel(files[top + i].name);
					if (root_label != NULL)
						strcpy(tmp, root_label);
					else
						strcpy(tmp, files[top + i].name);
					if (file_show > 0) {  //Does display mode include file details ?
						name_limit = 43 * 8;
					} else {  //Filenames are shown without file details
						name_limit = 71 * 8;
					}
				}
				if (name_limit) {  //Do we need to check name length ?
							int max_width = name_limit;

							//修复"原始 GBK 字节"的文件名(记忆卡/PS1存档): 非法 UTF-8
							//字节被逐字节显示为 CP437 符号(制表符/希腊字母乱码),
							//检测 GB2312 配对并转为 UTF-8(操作仍用原始名字)
							if (raw_gbk_to_utf8(tmp2, tmp))
								strcpy(tmp, tmp2);
							//修复"GBK 伪装 UTF-16"的旧文件名（显示为 ÖÐ 等 Latin-1 乱码）
							gbk_fake_to_utf8(tmp, tmp);
							if (files[top + i].stats.AttrFile & sceMcFileAttrSubdir)
								max_width -= 8;  //For folders, reserve one character for final '/'
							//选中行名字超宽 → 跑马灯滚动显示(支持任意长度, 50+字也不截断);
							//首尾各停留约 1.4 秒后循环; 未选中行 → 截断加 '~'
							{
								int full_w = utf8_display_width(tmp);

								if ((top + i == browser_sel) && full_w > max_width) {
									int span = full_w - max_width;
									int phase = scroll_px % (span + 384);

									if (phase < 192)
										phase = 0;
									else if (phase - 192 < span)
										phase -= 192;
									else
										phase = span;
									utf8_window(tmp, phase, max_width, tmp2);
									strcpy(tmp, tmp2);
									scroll_active = 1;
								} else
									utf8_truncate_width(tmp, max_width);
							}
						}

				if (files[top + i].stats.AttrFile & sceMcFileAttrSubdir && path[0] != 0)
					strcat(tmp, "/");
				if (mcTitle != NULL)
					printXY_sjis(mcTitle, x + 4, y, color, TRUE);
				else
					printXY(tmp, x + 4, y, color, TRUE, name_limit);
				if (file_show > 0) {
					//					unsigned int size = files[top+i].stats.fileSizeByte;
					u64 size = ((u64)files[top + i].stats.Reserve2 << 32) | files[top + i].stats.FileSizeByte;
					int scale = 0;  //0==Bytes, 1==KBytes, 2==MBytes, 3==GB
					char scale_s[6] = " KMGTP";
					PS2TIME timestamp = *(PS2TIME *)&files[top + i].stats._Modify;

					if (!size_valid)
						size = 0;
					if (!time_valid)
						memset((void *)&timestamp, 0, sizeof(timestamp));

					if (!size_valid || !(top + i))
						strcpy(tmp, "----- B");
					else if ((files[top + i].stats.AttrFile & sceMcFileAttrSubdir) && size == 0)
						strcpy(tmp, "    - B");
					else {
						while (size > 99999) {
							scale++;
							size /= 1024;
						}
						sprintf(tmp, "%5llu%cB", (unsigned long long)size, scale_s[scale]);
					}

					if (!time_valid || !(top + i))
						strcat(tmp, " ----.--.-- --:--:--");
					else {
						sprintf(tmp + strlen(tmp), " %04d.%02d.%02d %02d:%02d:%02d",
						        ((timestamp.year < 2256) ? timestamp.year : (timestamp.year - 256)),
						        timestamp.month,
						        timestamp.day,
						        timestamp.hour,
						        timestamp.min,
						        timestamp.sec);
					}

					printXY(tmp, x + 4 + 44 * FONT_WIDTH, y, color, TRUE, 0);
				}
				if (setting->FB_NoIcons) {  //if FileBrowser should not use icons
					if (marks[top + i])
						drawChar('*', x - 6, y, setting->color[COLOR_TEXT]);
				} else {  //if Icons must be used in front of file/folder names
					if (files[top + i].stats.AttrFile & sceMcFileAttrSubdir) {
						iconbase = ICON_FOLDER;
						iconcolr = COLOR_GRAPH1;
					} else {
						iconbase = ICON_FILE;
						if (genCmpFileExt(files[top + i].name, "ELF"))
							iconcolr = COLOR_GRAPH2;
						else if (
									genCmpFileExt(files[top + i].name, "TXT") || 
									genCmpFileExt(files[top + i].name, "CFG") || 
									genCmpFileExt(files[top + i].name, "CNF") || 
									genCmpFileExt(files[top + i].name, "INI") || 
									genCmpFileExt(files[top + i].name, "CHT") || 
									genCmpFileExt(files[top + i].name, "PBT") ||
									genCmpFileExt(files[top + i].name, "JS") ||
									genCmpFileExt(files[top + i].name, "LUA") ||
									genCmpFileExt(files[top + i].name, "XML") ||
									genCmpFileExt(files[top + i].name, "TOML") ||
									genCmpFileExt(files[top + i].name, "YAML") ||
									genCmpFileExt(files[top + i].name, "YML") ||
									genCmpFileExt(files[top + i].name, "ARG")
									)
							iconcolr = COLOR_GRAPH4;
						else
							iconcolr = COLOR_GRAPH3;
					}
					if (marks[top + i])
						iconbase += 2;
					drawChar(iconbase, x - 3 - FONT_WIDTH, y, setting->color[iconcolr]);
					drawChar(iconbase + 1, x - 3, y, setting->color[iconcolr]);
				}
				y += font_height;
			}                             //ends for, so all browser rows were fixed above
			if (browser_nfiles > rows) {  //if more files than available rows, use scrollbar
				drawFrame(SCREEN_WIDTH - SCREEN_MARGIN - LINE_THICKNESS * 8, Frame_start_y,
				          SCREEN_WIDTH - SCREEN_MARGIN, Frame_end_y, setting->color[COLOR_FRAME]);
				y0 = (Menu_end_y - Menu_start_y + 8) * ((double)top / browser_nfiles);
				y1 = (Menu_end_y - Menu_start_y + 8) * ((double)(top + rows) / browser_nfiles);
				drawOpSprite(setting->color[COLOR_FRAME],
				             SCREEN_WIDTH - SCREEN_MARGIN - LINE_THICKNESS * 6, (y0 + Menu_start_y - 4),
				             SCREEN_WIDTH - SCREEN_MARGIN - LINE_THICKNESS * 2, (y1 + Menu_start_y - 4));
			}                  //ends clause for scrollbar
			if (nclipFiles) {  //if Something in clipboard, emulate LED indicator
				u64 LED_colour, RIM_colour = GS_SETREG_RGBA(0, 0, 0, 0);
				int RIM_w = 4, LED_w = 6, indicator_w = LED_w + 2 * RIM_w;
				int x2 = SCREEN_WIDTH - SCREEN_MARGIN - 2, x1 = x2 - indicator_w;
				int y1 = Frame_start_y + 2, y2 = y1 + indicator_w;

				if (browser_cut)
					LED_colour = GS_SETREG_RGBA(0xC0, 0, 0, 0);  //Red LED == CUT
				else
					LED_colour = GS_SETREG_RGBA(0, 0xC0, 0, 0);  //Green LED == COPY
				drawOpSprite(RIM_colour, x1, y1, x2, y2);
				drawOpSprite(LED_colour, x1 + RIM_w, y1 + RIM_w, x2 - RIM_w, y2 - RIM_w);
			}  //ends clause for clipboard indicator
				if (browser_pushed) {
					char display_path[MAX_PATH];

					formatBrowserPathForDisplay(path, display_path);
					snprintf(msg0, sizeof(msg0), "%s", display_path);
				}

			//Tooltip section
			if (cnfmode) {  //cnfmode indicates configurable file selection
				if (swapKeys)
					sprintf(msg1, "\xFF"
					              "1:%s \xFF"
					              "0:%s \xFF"
					              "3:%s \xFF"
					              "2:",
					        LNG(OK), LNG(Cancel), LNG(Up));
				else
					sprintf(msg1, "\xFF"
					              "0:%s \xFF"
					              "1:%s \xFF"
					              "3:%s \xFF"
					              "2:",
					        LNG(OK), LNG(Cancel), LNG(Up));
				if (ext[0] == '*')
					strcat(msg1, "*->");
				strcat(msg1, cnfmode_extU[cnfmode]);
				if (ext[0] != '*')
					strcat(msg1, "->*");
				sprintf(tmp, " R2:%s", LNG(PathPad));
				strcat(msg1, tmp);
				if ((cnfmode == DIR_CNF) || (cnfmode == SAVE_CNF)) {  //modes using Start
					sprintf(tmp, " Start:%s", LNG(Choose));
					strcat(msg1, tmp);
				}
			} else {  // cnfmode == FALSE
				if (swapKeys)
					sprintf(msg1, "\xFF"
					              "1:%s \xFF"
					              "3:%s \xFF"
					              "0:%s \xFF"
					              "2:%s L1:%s R1:%s R2:%s Sel:%s",
					        LNG(OK), LNG(Up), LNG(Mark), LNG(RevMark),
					        LNG(Mode), LNG(Menu), LNG(PathPad), LNG(Exit));
				else
					sprintf(msg1, "\xFF"
					              "0:%s \xFF"
					              "3:%s \xFF"
					              "1:%s \xFF"
					              "2:%s L1:%s R1:%s R2:%s Sel:%s",
					        LNG(OK), LNG(Up), LNG(Mark), LNG(RevMark),
					        LNG(Mode), LNG(Menu), LNG(PathPad), LNG(Exit));
			}
			setScrTmp(msg0, msg1);
			if (vfreeSpace) {
				if (freeSpace >= 1024 * 1024)
					sprintf(tmp, "[%.1fMB %s]", (double)freeSpace / 1024 / 1024, LNG(free));
				else if (freeSpace >= 1024)
					sprintf(tmp, "[%.1fKB %s]", (double)freeSpace / 1024, LNG(free));
				else
					sprintf(tmp, "[%dB %s]", (int)freeSpace, LNG(free));
				ret = strlen(tmp);
				drawSprite(setting->color[COLOR_BACKGR],
				           SCREEN_WIDTH - SCREEN_MARGIN - (ret + 1) * FONT_WIDTH, (Menu_message_y - 1),
				           SCREEN_WIDTH - SCREEN_MARGIN, (Menu_message_y + FONT_HEIGHT));
				printXY(tmp,
				        SCREEN_WIDTH - SCREEN_MARGIN - ret * FONT_WIDTH,
				        (Menu_message_y),
				        setting->color[COLOR_SELECT], TRUE, 0);
			}
		}  //ends if(event||post_event)
		drawScr();
		post_event = event;
		event = 0;
	}  //ends while

	//Leaving the browser
	if (rv <= 0)
		LaunchArgsClear();
	unmountAll();
	return rv;
}
//------------------------------
//endfunc getFilePath
//--------------------------------------------------------------
static void submenu_func_GetSize(char *mess, char *path, FILEINFO *files)
{
	u64 size;
	u64 entry_size;
	int ret, i, text_pos, text_inc, sel = -1;
	char filepath[MAX_PATH];

	/*
	int test;
	iox_stat_t stats;
	PS2TIME *time;
*/

	drawMsg(LNG(Checking_Size));
	if (nmarks == 0) {
		size = getFileSize(path, &files[browser_sel]);
		sel = browser_sel;  //for stat checking
		if ((size != (u64)-1) && (files[browser_sel].stats.AttrFile & sceMcFileAttrSubdir)) {
			files[browser_sel].stats.FileSizeByte = (u32)size;
			files[browser_sel].stats.Reserve2 = (u32)(size >> 32);
		}
	} else {
		for (i = size = 0; i < browser_nfiles; i++) {
			if (marks[i]) {
				entry_size = getFileSize(path, &files[i]);
				if (entry_size == (u64)-1) {
					size = (u64)-1;
					break;
				}
				size += entry_size;
				if (files[i].stats.AttrFile & sceMcFileAttrSubdir) {
					files[i].stats.FileSizeByte = (u32)entry_size;
					files[i].stats.Reserve2 = (u32)(entry_size >> 32);
				}
				sel = i;  //for stat checking
			}
		}
	}
	DPRINTF("size result = %llu\r\n", (unsigned long long)size);
	if (size == (u64)-1) {
		strcpy(mess, LNG(Size_test_Failed));
		text_pos = strlen(mess);
	} else {
		text_pos = 0;
		if (size >= 1024 * 1024)
			sprintf(mess, "%s = %.1fMB%n", LNG(SIZE), (double)size / 1024 / 1024, &text_inc);
		else if (size >= 1024)
			sprintf(mess, "%s = %.1fKB%n", LNG(SIZE), (double)size / 1024, &text_inc);
		else
			sprintf(mess, "%s = %lluB%n", LNG(SIZE), (unsigned long long)size, &text_inc);
		text_pos += text_inc;
	}

	//----- Comment out this section to skip attributes entirely -----
	if ((nmarks < 2) && (sel >= 0)) {
		sprintf(filepath, "%s%s", path, files[sel].name);
		//----- Start of section for debug display of attributes -----
		/*
		printf("path =\"%s\"\r\n", path);
		printf("file =\"%s\"\r\n", files[sel].name);
		if	(!strncmp(filepath, "host:/", 6))
			makeHostPath(filepath, filepath);
		test = fileXioGetStat(filepath, &stats);
		printf("test = %d\r\n", test);
		printf("mode = %08X\r\n", stats.mode);
		printf("attr = %08X\r\n", stats.attr);
		printf("size = %08X\r\n", stats.size);
		time = (PS2TIME *) stats.ctime;
		printf("ctime = %04d.%02d.%02d %02d:%02d:%02d.%02d\r\n",
			time->year,time->month,time->day,
			time->hour,time->min,time->sec,time->unknown);
		time = (PS2TIME *) stats.atime;
		printf("atime = %04d.%02d.%02d %02d:%02d:%02d.%02d\r\n",
			time->year,time->month,time->day,
			time->hour,time->min,time->sec,time->unknown);
		time = (PS2TIME *) stats.mtime;
		printf("mtime = %04d.%02d.%02d %02d:%02d:%02d.%02d\r\n",
			time->year,time->month,time->day,
			time->hour,time->min,time->sec,time->unknown);
*/
		//----- End of section for debug display of attributes -----
		sprintf(mess + text_pos, " m=%04X %04d.%02d.%02d %02d:%02d:%02d%n",
		        files[sel].stats.AttrFile,
		        files[sel].stats._Modify.Year,
		        files[sel].stats._Modify.Month,
		        files[sel].stats._Modify.Day,
		        files[sel].stats._Modify.Hour,
		        files[sel].stats._Modify.Min,
		        files[sel].stats._Modify.Sec,
		        &text_inc);
		text_pos += text_inc;
		if (!strncmp(path, "mc", 2)) {
			mcGetInfo(path[2] - '0', 0, &mctype_PSx, NULL, NULL);
			mcSync(0, NULL, &ret);
			sprintf(mess + text_pos, " %s=%d%n", LNG(mctype), mctype_PSx, &text_inc);
			text_pos += text_inc;
#ifdef XFROM
		} else if (!strncmp(path, "xfrom", 5)) {
			xfromGetInfo(0, 0, &mctype_PSx, NULL, NULL);
			xfromSync(0, NULL, &ret);
			sprintf(mess + text_pos, " %s=%d%n", LNG(mctype), mctype_PSx, &text_inc);
			text_pos += text_inc;
#endif
		}
		//sprintf(mess+text_pos, " mcTsz=%d%n", files[sel].stats.fileSizeByte, &text_inc);
		u64 size = ((u64)files[sel].stats.Reserve2 << 32) | files[sel].stats.FileSizeByte;
		//Max length is 20 characters+NULL
		char sizeC[21] = {0};
		char *sizeP = &sizeC[21];
		do {
			*(--sizeP) = '0' + (size % 10);
		} while (size /= 10);
		sprintf(mess + text_pos, " mcTsz=%s%n", sizeP, &text_inc);
		text_pos += text_inc;
	}
	//----- End of sections that show attributes -----
	browser_pushed = FALSE;
}
//------------------------------
//endfunc submenu_func_GetSize
//--------------------------------------------------------------
static void subfunc_Paste(char *mess, char *path)
{
	char tmp[MAX_PATH], tmp1[MAX_PATH];
	int i, ret = -1, trace_vmc_paste;

	written_size = 0;
	PasteTime = Timer();  //Note initial pasting time
	if (!strcmp(path, clipPath))
		goto finished;
	trace_vmc_paste = (!strncmp(clipPath, "vmc", 3) || !strncmp(path, "vmc", 3));
	ret = prepareTransferDeviceStacks(clipPath, path);
	if (ret == TRANSFER_STACK_INCOMPATIBLE) {
		printf("[PASTE] incompatible stacks ret=%d src='%s' dst='%s'\n", ret, clipPath, path);
		ynDialog("Incompatible drivers to perform action");
		browser_pushed = FALSE;
		return;
	}
	if (ret < 0) {
		printf("[PASTE] prepare stacks failed ret=%d src='%s' dst='%s'\n", ret, clipPath, path);
		goto finished;
	}
	drawMsg(LNG(Pasting));
	if (waitForClipboardSourceDevice() < 0) {
		printf("[PASTE] source device unavailable after IOP reset src='%s' dst='%s'\n", clipPath, path);
		ret = -1;
		goto finished;
	}
	if (trace_vmc_paste)
		printf("[PASTE] start src='%s' dst='%s' items=%d mode=%d cut=%d\n",
		       clipPath, path, nclipFiles, PasteMode, browser_cut);

	for (i = 0; i < nclipFiles; i++) {
		strcpy(tmp, clipFiles[i].name);
		if (clipFiles[i].stats.AttrFile & sceMcFileAttrSubdir)
			strcat(tmp, "/");
		sprintf(tmp1, " %s", LNG(pasting));
		strcat(tmp, tmp1);
		drawMsg(tmp);
		PM_flag[0] = PM_NORMAL;  //Always use normal mode at top level
		PM_file[0] = -1;         //Thus no attribute file is used here
		if (trace_vmc_paste)
			printf("[PASTE] item src='%s' dst='%s' name='%s' index=%d/%d attr=0x%x size=%u:%u\n",
			       clipPath, path, clipFiles[i].name, i + 1, nclipFiles,
			       clipFiles[i].stats.AttrFile, clipFiles[i].stats.Reserve2, clipFiles[i].stats.FileSizeByte);
		ret = copy(path, clipPath, clipFiles[i], 0);
		if (ret < 0) {
			printf("[PASTE] copy failed ret=%d src='%s' dst='%s' item='%s' index=%d/%d mode=%d cut=%d\n",
			       ret, clipPath, path, clipFiles[i].name, i + 1, nclipFiles, PasteMode, browser_cut);
			break;
		}
	}
	if ((ret >= 0) && browser_cut) {
		for (i = 0; i < nclipFiles; i++) {
			if (filerConfirmExploitDelete(clipPath, &clipFiles[i]) < 0) {
				ret = -1;
				break;
			}
			ret = delete (clipPath, &clipFiles[i]);
			if (ret < 0)
				break;
		}
	}

//	unmountAll(); //disabled to avoid interference with VMC implementation

finished:
	if (ret < 0) {
		printf("[PASTE] failed ret=%d src='%s' dst='%s' mode=%d cut=%d\n",
		       ret, clipPath, path, PasteMode, browser_cut);
		strcpy(mess, LNG(Paste_Failed));
		browser_pushed = FALSE;
	} else {
		if (browser_cut)
			nclipFiles = 0;
		markMx4sioDestinationAfterWrite(path);
	}
	browser_cd = TRUE;
}
//------------------------------
//endfunc subfunc_Paste
//--------------------------------------------------------------
static void submenu_func_Paste(char *mess, char *path)
{
	if (new_pad & PAD_SQUARE)
		PasteMode = PM_RENAME;
	else
		PasteMode = PM_NORMAL;
	subfunc_Paste(mess, path);
}
//------------------------------
//endfunc submenu_func_Paste
//--------------------------------------------------------------
static void submenu_func_psuPaste(char *mess, char *path)
{
	int psu_action = classifyPsuAction(path);

	if (psu_action == PSU_ACTION_EXTRACT) {
		PasteMode = PM_PSU_RESTORE;
	} else if (psu_action == PSU_ACTION_CREATE) {
		PasteMode = PM_PSU_BACKUP;
	} else {
		strcpy(mess, LNG(Paste_Failed));
		browser_pushed = FALSE;
		return;
	}
	subfunc_Paste(mess, path);
}
//------------------------------
//endfunc submenu_func_psuPaste
//--------------------------------------------------------------
//End of file: filer_browser.c
//--------------------------------------------------------------
