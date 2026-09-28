//---------------------------------------------------------------------------
// File name:   config_network.c
//---------------------------------------------------------------------------
#include "config_private.h"
#include <stdlib.h>
#ifdef SMB
#include <ps2smb.h>
#endif

//---------------------------------------------------------------------------
// Network settings GUI by Slam-Tilt
// The SMB server settings (SMB.CNF) are edited here as well, next to
// the PS2-side IP settings, and are saved to SMB.CNF in the same
// directory as the chosen IPCONFIG.DAT save target.
//---------------------------------------------------------------------------
static void saveNetworkSettings(char *Message, const char *target_path)
{
	char firstline[50];
	int out_fd, in_fd;
	int ret = 0, i = 0;
	int size, sizeleft = 0;
	char *ipconfigfile = 0;
	char path[MAX_PATH];

	// Default message, will get updated if save is sucessfull
	sprintf(Message, "%s", LNG(Saved_Failed));

	sprintf(firstline, "%s %s %s\n\r", ip, netmask, gw);

	if (target_path == NULL || target_path[0] == '\0')
		return;



	// Preserve any existing data beyond the first line of the selected target.

	if (genFixPath(target_path, path) >= 0)
		in_fd = genOpen(path, FIO_O_RDONLY);
	else
		in_fd = -1;

	if (in_fd >= 0) {

		size = genLseek(in_fd, 0, SEEK_END);
		DPRINTF("%s: size of existing file is %ibytes\n\r", __func__, size);

		if (size > 0 && (ipconfigfile = (char *)memalign(64, size)) != NULL) {
			int read_size;

			genLseek(in_fd, 0, SEEK_SET);
			read_size = genRead(in_fd, ipconfigfile, size);
			if (read_size > 0) {
				size = read_size;
				for (i = 0; i < size && ipconfigfile[i] != '\r' && ipconfigfile[i] != '\n'; i++)
					;
				while (i < size && (ipconfigfile[i] == '\r' || ipconfigfile[i] == '\n'))
					i++;
				sizeleft = size - i;
			}
		}

		genClose(in_fd);
	}

	// Writing the data out

	configEnsureSysconfDir(target_path);
	if (genFixPath(target_path, path) < 0) {
		if (ipconfigfile != NULL)
			free(ipconfigfile);
		return;
	}
	out_fd = genOpen(path, FIO_O_WRONLY | FIO_O_TRUNC | FIO_O_CREAT);
	if (out_fd >= 0) {
		mcSync(0, NULL, &ret);
		genWrite(out_fd, firstline, strlen(firstline));
		mcSync(0, NULL, &ret);

		// If we have any extra data, spit that out too.
		if (sizeleft > 0) {
			mcSync(0, NULL, &ret);
			genWrite(out_fd, &ipconfigfile[i], sizeleft);
			mcSync(0, NULL, &ret);
		}

		snprintf(Message, MAX_PATH, "%s %.*s", LNG(Saved), MAX_PATH - 4, target_path);
		snprintf(LoadedIPConfigPath, sizeof(LoadedIPConfigPath), "%s", target_path);

		genClose(out_fd);
	}
	if (ipconfigfile != NULL)
		free(ipconfigfile);
}
//---------------------------------------------------------------------------
// Convert IP string to numbers
//---------------------------------------------------------------------------
static void ipStringToOctet(char *ip, int ip_octet[4])
{

	// This takes a string (ip) representing an IP address and converts it
	// into an array of ints (ip_octet)
	// Rewritten 22/10/05

	char oct_str[5];
	int oct_cnt, i, oct_len;

	oct_cnt = 0;
	oct_len = 0;
	oct_str[0] = '\0';

	for (i = 0; ((i <= strlen(ip)) && (oct_cnt < 4)); i++) {
		if ((ip[i] == '.') || (i == strlen(ip))) {
			ip_octet[oct_cnt] = atoi(oct_str);
			oct_cnt++;
			oct_len = 0;
			oct_str[0] = '\0';
		} else if (oct_len < (int)sizeof(oct_str) - 1) {
			oct_str[oct_len++] = ip[i];
			oct_str[oct_len] = '\0';
		}
	}
}
//---------------------------------------------------------------------------
static data_ip_struct BuildOctets(char *ip, char *nm, char *gw)
{

	// Populate 3 arrays with the ip address (as ints)

	data_ip_struct iplist;

	ipStringToOctet(ip, iplist.ip);
	ipStringToOctet(nm, iplist.nm);
	ipStringToOctet(gw, iplist.gw);

	return (iplist);
}
//---------------------------------------------------------------------------
//The SMB port is edited digit by digit (00001-65535); after every
//change the digits are re-assembled, clamped into range and split
//again, so the display can never hold an invalid port.
static void smbPortToDigits(int port, int digits[5])
{
	int i, divisor;

	if (port < 1)
		port = 1;
	if (port > 65535)
		port = 65535;
	divisor = 10000;
	for (i = 0; i < 5; i++) {
		digits[i] = (port / divisor) % 10;
		divisor /= 10;
	}
}
//------------------------------
//endfunc smbPortToDigits
//--------------------------------------------------------------
static int smbDigitsToPort(const int digits[5])
{
	int i, port = 0;

	for (i = 0; i < 5; i++)
		port = port * 10 + digits[i];
	if (port < 1)
		port = 1;
	if (port > 65535)
		port = 65535;
	return port;
}
//------------------------------
//endfunc smbDigitsToPort
//--------------------------------------------------------------
#ifdef SMB
//Derive the SMB.CNF path from the chosen IPCONFIG.DAT save target,
//so both files always land in the same directory.
static void deriveSmbCnfPath(const char *ipconfig_path, char *smb_path, size_t size)
{
	static const char ipconfig_name[] = "IPCONFIG.DAT";
	size_t len, dir_len;

	if (ipconfig_path == NULL || smb_path == NULL || size == 0)
		return;
	len = strlen(ipconfig_path);
	if (len >= sizeof(ipconfig_name) - 1 &&
	    !strcmp(ipconfig_path + len - (sizeof(ipconfig_name) - 1), ipconfig_name)) {
		dir_len = len - (sizeof(ipconfig_name) - 1);
		if (dir_len >= size)
			dir_len = size - 1;
		memcpy(smb_path, ipconfig_path, dir_len);
		snprintf(smb_path + dir_len, size - dir_len, "SMB.CNF");
	} else
		snprintf(smb_path, size, "%s", ipconfig_path);
}
//------------------------------
//endfunc deriveSmbCnfPath
//--------------------------------------------------------------
//Write the SMB server settings to SMB.CNF. Returns 1 on success.
static int saveSMBSettings(const char *target_path, const data_ip_struct *smb_ip,
                           int port, const char *share, const char *user,
                           const char *password)
{
	char path[MAX_PATH];
	char server_ip[20];
	char buf[512];
	int out_fd, ret = 0, len;

	if (target_path == NULL || target_path[0] == '\0')
		return 0;

	snprintf(server_ip, sizeof(server_ip), "%i.%i.%i.%i",
	         smb_ip->ip[0], smb_ip->ip[1], smb_ip->ip[2], smb_ip->ip[3]);
	len = snprintf(buf, sizeof(buf),
	               "SERVER_IP = %s\n"
	               "SERVER_PORT = %d\n"
	               "SHARE = %s\n"
	               "USER = %s\n"
	               "PASSWORD = %s\n"
	               "PASSWORD_TYPE = %d\n",
	               server_ip, port, share, user, password,
	               password[0] ? HASHED_PASSWORD : NO_PASSWORD);
	if (len <= 0 || len >= (int)sizeof(buf))
		return 0;

	configEnsureSysconfDir(target_path);
	if (genFixPath(target_path, path) < 0)
		return 0;
	out_fd = genOpen(path, FIO_O_WRONLY | FIO_O_TRUNC | FIO_O_CREAT);
	if (out_fd < 0)
		return 0;

	mcSync(0, NULL, &ret);
	genWrite(out_fd, buf, len);
	mcSync(0, NULL, &ret);
	genClose(out_fd);
	return 1;
}
//------------------------------
//endfunc saveSMBSettings
//--------------------------------------------------------------
#endif  //SMB
//---------------------------------------------------------------------------
enum CONFIG_NET {
	CONFIG_NET_FIRST = 1,
	CONFIG_NET_IP = CONFIG_NET_FIRST,
	CONFIG_NET_NM,
	CONFIG_NET_GW,
#ifdef SMB
	CONFIG_NET_SMB_IP,
	CONFIG_NET_SMB_PORT,
	CONFIG_NET_SMB_SHARE,
	CONFIG_NET_SMB_USER,
	CONFIG_NET_SMB_PASS,
#endif

	//Settings after IP addresses
	CONFIG_NET_AFT_IP,
	CONFIG_NET_SAVE = CONFIG_NET_AFT_IP,
	CONFIG_NET_RETURN,

	CONFIG_NET_COUNT
};

static int configNetOctetRow(int s)
{
	if (s == CONFIG_NET_IP || s == CONFIG_NET_NM || s == CONFIG_NET_GW)
		return 1;
#ifdef SMB
	if (s == CONFIG_NET_SMB_IP)
		return 1;
#endif
	return 0;
}

static int configNetPortRow(int s)
{
#ifdef SMB
	return (s == CONFIG_NET_SMB_PORT);
#else
	return 0;
#endif
}

static int configNetTextRow(int s)
{
#ifdef SMB
	return (s == CONFIG_NET_SMB_SHARE || s == CONFIG_NET_SMB_USER ||
	        s == CONFIG_NET_SMB_PASS);
#else
	return 0;
#endif
}

//Highest column index (l) valid for a row: 5 for octet rows (4 octets),
//6 for the port row (5 digits) and 1 for rows without column editing.
static int configNetMaxCol(int s)
{
	if (configNetPortRow(s))
		return 6;
	if (configNetOctetRow(s))
		return 5;
	return 1;
}

static int *configNetOctets(int s, data_ip_struct *ipdata, data_ip_struct *smbdata)
{
	if (s == CONFIG_NET_IP)
		return ipdata->ip;
	if (s == CONFIG_NET_NM)
		return ipdata->nm;
	if (s == CONFIG_NET_GW)
		return ipdata->gw;
#ifdef SMB
	return smbdata->ip;
#else
	return ipdata->ip;
#endif
}

void Config_Network(void)
{
	// Menu System for Network Settings Page.

	int s, l;
	int x, y;
	int event, post_event = 0;
	int len;
	char c[MAX_PATH];
	char value[MAX_PATH];
	data_ip_struct ipdata;
	data_ip_struct smbdata;
	int smb_port_digits[5];
	char NetMsg[MAX_PATH] = "";
	char save_override_path[MAX_PATH];
	char save_cwd_path[MAX_PATH];
	char save_sysconf_path[MAX_PATH];
	int has_override_path;
	int save_target;
	char *save_path;
#ifdef SMB
	smb_cnf_t smbcnf;
	char smb_share_v[64];
	char smb_user_v[64];
	char smb_pass_v[64];
	int smb_cnf_existed;
#endif

	event = 1;  //event = initial entry
	s = CONFIG_NET_FIRST;
	l = 1;
	ipdata = BuildOctets(ip, netmask, gw);
	configBuildSaveTargets(save_override_path, sizeof(save_override_path),
	                       save_cwd_path, sizeof(save_cwd_path),
	                       save_sysconf_path, sizeof(save_sysconf_path),
	                       "IPCONFIG.DAT", LoadedIPConfigPath, &has_override_path);
#ifdef SMB
	//Load the current SMB.CNF (if any) into the editable fields.
	smb_cnf_existed = (smbLoadCnf(&smbcnf) != 0);
	ipStringToOctet(smbcnf.server_ip, smbdata.ip);
	smbPortToDigits(smbcnf.server_port, smb_port_digits);
	strcpy(smb_share_v, smbcnf.share);
	strcpy(smb_user_v, smbcnf.user);
	strcpy(smb_pass_v, smbcnf.password);
#endif

	while (1) {
		//Pad response section
		waitPadReady(0, 0);
		if (readpad()) {
			if (new_pad & PAD_UP) {
				event |= 2;  //event |= valid pad command
				if (s != CONFIG_NET_FIRST)
					s--;
				else {
					s = CONFIG_NET_RETURN;
					l = 1;
				}
				if (l > configNetMaxCol(s))
					l = 1;
			} else if (new_pad & PAD_DOWN) {
				event |= 2;  //event |= valid pad command
				if (s != CONFIG_NET_COUNT - 1)
					s++;
				else
					s = CONFIG_NET_FIRST;
				if (l > configNetMaxCol(s))
					l = 1;
			} else if (new_pad & PAD_LEFT) {
				event |= 2;  //event |= valid pad command
				if (l > 1)
					l--;
			} else if (new_pad & PAD_RIGHT) {
				event |= 2;  //event |= valid pad command
				if (l < configNetMaxCol(s))
					l++;
			} else if ((!swapKeys && new_pad & PAD_CROSS) || (swapKeys && new_pad & PAD_CIRCLE)) {
				event |= 2;  //event |= valid pad command
				if (configNetOctetRow(s) && (l > 1)) {
					int *octets = configNetOctets(s, &ipdata, &smbdata);

					if (octets[l - 2] > 0)
						octets[l - 2]--;
				} else if (configNetPortRow(s) && (l > 1)) {
					smb_port_digits[l - 2] = (smb_port_digits[l - 2] + 9) % 10;
					smbPortToDigits(smbDigitsToPort(smb_port_digits), smb_port_digits);
				}
			} else if ((swapKeys && new_pad & PAD_CROSS) || (!swapKeys && new_pad & PAD_CIRCLE)) {
				event |= 2;  //event |= valid pad command
				if (configNetOctetRow(s) && (l > 1)) {
					int *octets = configNetOctets(s, &ipdata, &smbdata);

					if (octets[l - 2] < 255)
						octets[l - 2]++;
				} else if (configNetPortRow(s) && (l > 1)) {
					smb_port_digits[l - 2] = (smb_port_digits[l - 2] + 1) % 10;
					smbPortToDigits(smbDigitsToPort(smb_port_digits), smb_port_digits);
				}
#ifdef SMB
				else if (configNetTextRow(s)) {
					//Share/user/password are edited with the pop-up keyboard.
					char kbd_tmp[64];
					char *dst;

					if (s == CONFIG_NET_SMB_SHARE)
						dst = smb_share_v;
					else if (s == CONFIG_NET_SMB_USER)
						dst = smb_user_v;
					else
						dst = smb_pass_v;
					strcpy(kbd_tmp, dst);
					if (keyboard(kbd_tmp, sizeof(kbd_tmp)) >= 0)
						strcpy(dst, kbd_tmp);
				}
#endif
				else if (s == CONFIG_NET_SAVE) {
					save_target = configSaveTargetPrompt(save_override_path, save_cwd_path, save_sysconf_path, LoadedIPConfigPath, has_override_path);
					if (save_target != CONFIG_SAVE_TARGET_CANCEL) {
						sprintf(ip, "%i.%i.%i.%i", ipdata.ip[0], ipdata.ip[1], ipdata.ip[2], ipdata.ip[3]);
						sprintf(netmask, "%i.%i.%i.%i", ipdata.nm[0], ipdata.nm[1], ipdata.nm[2], ipdata.nm[3]);
						sprintf(gw, "%i.%i.%i.%i", ipdata.gw[0], ipdata.gw[1], ipdata.gw[2], ipdata.gw[3]);

						if (save_target == CONFIG_SAVE_TARGET_OVERRIDE)
							save_path = save_override_path;
						else if (save_target == CONFIG_SAVE_TARGET_CWD)
							save_path = save_cwd_path;
						else
							save_path = save_sysconf_path;
						configRefreshSaveTargetForWrite(save_target, save_path, MAX_PATH, "IPCONFIG.DAT", LoadedIPConfigPath);
						saveNetworkSettings(NetMsg, save_path);
#ifdef SMB
						//SMB.CNF goes to the same directory as IPCONFIG.DAT.
						//When no share is set and no SMB.CNF existed, skip it
						//instead of writing an unusable file.
						if (smb_share_v[0] != '\0' || smb_cnf_existed) {
							char smb_path[MAX_PATH];

							deriveSmbCnfPath(save_path, smb_path, sizeof(smb_path));
							if (saveSMBSettings(smb_path, &smbdata,
							                    smbDigitsToPort(smb_port_digits),
							                    smb_share_v, smb_user_v, smb_pass_v)) {
								smbDisconnect();  //next visit to smb: reconnects with the new settings
								if (strlen(NetMsg) + 12 < sizeof(NetMsg))
									strcat(NetMsg, " +SMB.CNF");
							} else
								snprintf(NetMsg, sizeof(NetMsg), "SMB.CNF %s", LNG(Failed_writing));
						}
#endif
					}
				} else  //s == CONFIG_NET_RETURN
					return;
			} else if (new_pad & PAD_TRIANGLE)
				return;
		}

		if (event || post_event) {  //NB: We need to update two frame buffers per event

			//Display section
			clrScr(setting->color[COLOR_BACKGR]);

			x = Menu_start_x;
			y = Menu_start_y;

			printXY(LNG(NETWORK_SETTINGS), x, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			y += FONT_HEIGHT / 2;

			len = (strlen(LNG(IP_Address)) + 5 > strlen(LNG(Netmask)) + 5) ?
			          strlen(LNG(IP_Address)) + 5 :
			          strlen(LNG(Netmask)) + 5;
			len = (len > strlen(LNG(Gateway)) + 5) ? len : strlen(LNG(Gateway)) + 5;
#ifdef SMB
			len = (len > strlen(LNG(SMB_Server_IP)) + 5) ? len : strlen(LNG(SMB_Server_IP)) + 5;
			len = (len > strlen(LNG(SMB_Port)) + 5) ? len : strlen(LNG(SMB_Port)) + 5;
			len = (len > strlen(LNG(SMB_Share)) + 5) ? len : strlen(LNG(SMB_Share)) + 5;
			len = (len > strlen(LNG(SMB_User)) + 5) ? len : strlen(LNG(SMB_User)) + 5;
			len = (len > strlen(LNG(SMB_Password)) + 5) ? len : strlen(LNG(SMB_Password)) + 5;
#endif
			sprintf(c, "%s:", LNG(IP_Address));
			printXY(c, x + 2 * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			sprintf(c, "%.3i . %.3i . %.3i . %.3i", ipdata.ip[0], ipdata.ip[1], ipdata.ip[2], ipdata.ip[3]);
			printXY(c, x + len * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			sprintf(c, "%s:", LNG(Netmask));
			printXY(c, x + 2 * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			sprintf(c, "%.3i . %.3i . %.3i . %.3i", ipdata.nm[0], ipdata.nm[1], ipdata.nm[2], ipdata.nm[3]);
			printXY(c, x + len * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			sprintf(c, "%s:", LNG(Gateway));
			printXY(c, x + 2 * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			sprintf(c, "%.3i . %.3i . %.3i . %.3i", ipdata.gw[0], ipdata.gw[1], ipdata.gw[2], ipdata.gw[3]);
			printXY(c, x + len * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

#ifdef SMB
			sprintf(c, "%s:", LNG(SMB_Server_IP));
			printXY(c, x + 2 * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			sprintf(c, "%.3i . %.3i . %.3i . %.3i", smbdata.ip[0], smbdata.ip[1], smbdata.ip[2], smbdata.ip[3]);
			printXY(c, x + len * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			sprintf(c, "%s:", LNG(SMB_Port));
			printXY(c, x + 2 * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			sprintf(c, "%i%i%i%i%i", smb_port_digits[0], smb_port_digits[1],
			        smb_port_digits[2], smb_port_digits[3], smb_port_digits[4]);
			printXY(c, x + len * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			sprintf(c, "%s:", LNG(SMB_Share));
			printXY(c, x + 2 * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			printXY(smb_share_v[0] ? smb_share_v : "(未设置)", x + len * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			sprintf(c, "%s:", LNG(SMB_User));
			printXY(c, x + 2 * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			printXY(smb_user_v[0] ? smb_user_v : "(未设置)", x + len * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			sprintf(c, "%s:", LNG(SMB_Password));
			printXY(c, x + 2 * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			printXY(smb_pass_v[0] ? smb_pass_v : "(未设置)", x + len * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;
#endif

			y += FONT_HEIGHT / 2;

			snprintf(value, sizeof(value), "...");
			configFormatLabelValue(c, sizeof(c), LNG(Save_to), value);
			printXY(c, x, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			y += FONT_HEIGHT / 2;
			sprintf(c, "  %s", LNG(RETURN));
			printXY(c, x, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			//Cursor positioning section
			y = Menu_start_y + s * FONT_HEIGHT + FONT_HEIGHT / 2;

			if (s >= CONFIG_NET_AFT_IP)
				y += FONT_HEIGHT / 2;
			if (s >= CONFIG_NET_RETURN)
				y += FONT_HEIGHT / 2;
			if (l > 1) {
				if (configNetPortRow(s))
					x += (len - 1) * FONT_WIDTH - 1 + (l - 2) * FONT_WIDTH;
				else
					x += (len - 1) * FONT_WIDTH - 1 + (l - 2) * 6 * FONT_WIDTH;
			}
			drawChar(LEFT_CUR, x, y, setting->color[COLOR_TEXT]);

			//Tooltip section
			if ((configNetOctetRow(s) || configNetPortRow(s)) && (l == 1)) {
				len = sprintf(c, "%s", LNG(Right_DPad_to_Edit));
			} else if (configNetOctetRow(s) || configNetPortRow(s)) {
				if (swapKeys)
					len = sprintf(c, "\xFF"
					                 "1:%s \xFF"
					                 "0:%s",
					              LNG(Add), LNG(Subtract));
				else
					len = sprintf(c, "\xFF"
					                 "0:%s \xFF"
					                 "1:%s",
					              LNG(Add), LNG(Subtract));
			} else if (configNetTextRow(s)) {
				if (swapKeys)
					len = sprintf(c, "\xFF"
					                 "1:%s",
					              LNG(Edit));
				else
					len = sprintf(c, "\xFF"
					                 "0:%s",
					              LNG(Edit));
			} else if (s == CONFIG_NET_SAVE) {
				if (swapKeys)
					len = sprintf(c, "\xFF"
					                 "1:%s",
					              LNG(Select));
				else
					len = sprintf(c, "\xFF"
					                 "0:%s",
					              LNG(Select));
			} else {
				if (swapKeys)
					len = sprintf(c, "\xFF"
					                 "1:%s",
					              LNG(OK));
				else
					len = sprintf(c, "\xFF"
					                 "0:%s",
					              LNG(OK));
			}
			sprintf(&c[len], " \xFF"
			                 "3:%s",
			        LNG(Return));
			setScrTmp(NetMsg, c);
		}  //ends if(event||post_event)
		drawScr();
		post_event = event;
		event = 0;

	}  //ends while
}  //ends Config_Network
//---------------------------------------------------------------------------
// End of file: config_network.c
//---------------------------------------------------------------------------
