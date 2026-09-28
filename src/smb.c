//--------------------------------------------------------------
//File name:   smb.c
//--------------------------------------------------------------
//SMB share support, using the module set proven in SNESticleAurora:
//the classic pre-netman stack (ps2dev9 + ps2ip lwIP 2.0.3 + smap-ps2ip
//+ ps2ips) and the patched smbman that tolerates embedded SMB1
//servers (end-of-search mapping, open-size fallbacks, strict path
//normalization and lossless UTF-8 filenames).
//
//smbman exposes the logged-on share as the "smb:" iomanX device, so
//the filer browses it directly once the share is open. Credentials
//are read from SMB.CNF (SERVER_IP/PORT/SHARE/USER/PASSWORD/
//PASSWORD_TYPE), searched next to the launched ELF and then in
//mc?:/SYS-CONF/. The PS2 IP/netmask/gateway come from the standard
//IPCONFIG.DAT (Network Settings GUI) and are handed to smap-ps2ip
//as module arguments. The Network Settings GUI also edits the SMB
//server settings and writes them back to SMB.CNF via smbLoadCnf().
//--------------------------------------------------------------
#include "launchelf.h"
#include "init.h"
#include <fileXio.h>
#include <ps2smb.h>

#define IMPORT_BIN2C(_n) \
	extern u8 _n[];      \
	extern int size_##_n

IMPORT_BIN2C(smb_ps2ip_irx);
IMPORT_BIN2C(smb_smap_irx);
IMPORT_BIN2C(smb_ps2ips_irx);
IMPORT_BIN2C(smbman_irx);

enum SMB_STATE_E {
	SMB_STATE_IDLE = 0,
	SMB_STATE_CONNECTED
};

static char smb_server_ip[32] = "192.168.2.1";
static int smb_server_port = 445;
static char smb_share[64] = "";
static char smb_user[64] = "";
static char smb_password[64] = "";
static int smb_password_type = NO_PASSWORD;

static u8 have_smb_ps2ip = 0;
static u8 have_smb_smap = 0;
static u8 have_smb_ps2ips = 0;
static u8 have_smbman = 0;
static u8 smb_state = SMB_STATE_IDLE;

int smb_ready = 0;  //share open, "smb:" device browsable
char smb_status_msg[MAX_PATH] = "";

//--------------------------------------------------------------
static char *smbTrim(char *text)
{
	char *end;

	while (*text && (*text == ' ' || *text == '\t' ||
	                 *text == '\r' || *text == '\n'))
		text++;
	end = text + strlen(text);
	while (end > text && (end[-1] == ' ' || end[-1] == '\t' ||
	                      end[-1] == '\r' || end[-1] == '\n'))
		*--end = '\0';
	return text;
}
//------------------------------
//endfunc smbTrim
//--------------------------------------------------------------
static void smbUnquote(char *text)
{
	size_t length = strlen(text);

	if (length >= 2 &&
	    ((text[0] == '"' && text[length - 1] == '"') ||
	     (text[0] == '\'' && text[length - 1] == '\''))) {
		memmove(text, text + 1, length - 2);
		text[length - 2] = '\0';
	}
}
//------------------------------
//endfunc smbUnquote
//--------------------------------------------------------------
static int smbCopyValue(char *destination, int destinationSize, const char *value)
{
	int length = strlen(value);

	if (length >= destinationSize)
		return -1;
	memcpy(destination, value, length + 1);
	return 0;
}
//------------------------------
//endfunc smbCopyValue
//--------------------------------------------------------------
static int smbValidIPv4(const char *address)
{
	unsigned int a, b, c, d;
	char tail;

	return sscanf(address, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) == 4 &&
	       a <= 255 && b <= 255 && c <= 255 && d <= 255;
}
//------------------------------
//endfunc smbValidIPv4
//--------------------------------------------------------------
//Returns 1 when a usable config was parsed, 0 when no SMB.CNF exists
//and -1 when one exists but is unusable. Reads through genOpen so it
//works from any device wLaunchELF itself was launched from.
static int smbReadConfigFile(const char *path, smb_cnf_t *cnf)
{
	int fd, len, pos, linelen, passwordTypeSeen = 0;
	char buf[2048];
	char line[512];
	char *key, *value, *equals;

	//Defaults. PS2-side network settings are not part of SMB.CNF here:
	//they come from the standard IPCONFIG.DAT and its settings GUI.
	strcpy(cnf->server_ip, "192.168.2.1");
	cnf->server_port = 445;
	cnf->share[0] = '\0';
	cnf->user[0] = '\0';
	cnf->password[0] = '\0';
	cnf->password_type = NO_PASSWORD;

	fd = genOpen(path, FIO_O_RDONLY);
	if (fd < 0)
		return 0;
	len = genRead(fd, buf, sizeof(buf) - 1);
	genClose(fd);
	if (len <= 0)
		return 0;
	buf[len] = '\0';

	pos = 0;
	while (pos < len) {
		linelen = 0;
		while (pos < len && buf[pos] != '\n' && linelen < (int)sizeof(line) - 1)
			line[linelen++] = buf[pos++];
		line[linelen] = '\0';
		if (pos < len && buf[pos] == '\n')
			pos++;

		key = smbTrim(line);
		if (!key[0] || key[0] == '#' || key[0] == ';')
			continue;
		equals = strchr(key, '=');
		if (!equals)
			continue;  //ignore malformed lines instead of failing
		*equals = '\0';
		value = smbTrim(equals + 1);
		key = smbTrim(key);
		smbUnquote(value);

		if (!strcasecmp(key, "SERVER_IP") || !strcasecmp(key, "smbServer_IP"))
			smbCopyValue(cnf->server_ip, sizeof(cnf->server_ip), value);
		else if (!strcasecmp(key, "SERVER_PORT") || !strcasecmp(key, "smbServer_Port"))
			cnf->server_port = atoi(value);
		else if (!strcasecmp(key, "SHARE") || !strcasecmp(key, "smbShare"))
			smbCopyValue(cnf->share, sizeof(cnf->share), value);
		else if (!strcasecmp(key, "USER") || !strcasecmp(key, "smbUsername"))
			smbCopyValue(cnf->user, sizeof(cnf->user), value);
		else if (!strcasecmp(key, "PASSWORD") || !strcasecmp(key, "smbPassword"))
			smbCopyValue(cnf->password, sizeof(cnf->password), value);
		else if (!strcasecmp(key, "PASSWORD_TYPE") || !strcasecmp(key, "smbPasswordType")) {
			cnf->password_type = atoi(value);
			passwordTypeSeen = 1;
		}
		//Unknown keys are ignored, so OPL-style smb.cnf files also load.
	}

	if (!cnf->password[0])
		cnf->password_type = NO_PASSWORD;
	else if (!passwordTypeSeen)
		cnf->password_type = HASHED_PASSWORD;

	if (!smbValidIPv4(cnf->server_ip) ||
	    cnf->server_port < 1 || cnf->server_port > 65535 ||
	    !cnf->share[0] ||
	    strchr(cnf->share, '/') || strchr(cnf->share, '\\'))
		return -1;
	return 1;
}
//------------------------------
//endfunc smbReadConfigFile
//--------------------------------------------------------------
//Search order mirrors getIpConfig(): next to the launched ELF, then
//mc?:/SYS-CONF/ (preferring the slot the ELF was launched from).
static int smbSearchConfig(smb_cnf_t *cnf)
{
	char path[MAX_PATH];
	size_t dir_len;
	int port_ix, preferred_port, ports_to_try[2];
	int result;

	dir_len = strnlen(LaunchElfDir, sizeof(path));
	if (dir_len < sizeof(path) && (dir_len + sizeof("SMB.CNF")) <= sizeof(path)) {
		memcpy(path, LaunchElfDir, dir_len);
		memcpy(path + dir_len, "SMB.CNF", sizeof("SMB.CNF"));
		result = smbReadConfigFile(path, cnf);
		if (result != 0)
			return result;
	}

	preferred_port = 0;
	if (!strncmp(LaunchElfDir, "mc1", 3))
		preferred_port = 1;
	ports_to_try[0] = preferred_port;
	ports_to_try[1] = preferred_port ^ 1;
	for (port_ix = 0; port_ix < 2; port_ix++) {
		snprintf(path, sizeof(path), "mc%d:/SYS-CONF/SMB.CNF", ports_to_try[port_ix]);
		result = smbReadConfigFile(path, cnf);
		if (result != 0)
			return result;
	}
	return 0;
}
//------------------------------
//endfunc smbSearchConfig
//--------------------------------------------------------------
//Public: load SMB.CNF from the standard search paths into *cnf, for
//the Network Settings GUI. Returns 1 usable, 0 not found, -1 invalid.
int smbLoadCnf(smb_cnf_t *cnf)
{
	return smbSearchConfig(cnf);
}
//------------------------------
//endfunc smbLoadCnf
//--------------------------------------------------------------
static int smbLoadConfig(void)
{
	smb_cnf_t cnf;
	int result = smbSearchConfig(&cnf);

	if (result <= 0)
		return result;
	strcpy(smb_server_ip, cnf.server_ip);
	smb_server_port = cnf.server_port;
	strcpy(smb_share, cnf.share);
	strcpy(smb_user, cnf.user);
	strcpy(smb_password, cnf.password);
	smb_password_type = cnf.password_type;
	return 1;
}
//------------------------------
//endfunc smbLoadConfig
//--------------------------------------------------------------
//Classic pre-netman stack, SNESticleAurora order:
//ps2dev9 -> ps2ip -> smap-ps2ip -> ps2ips -> smbman.
//smap-ps2ip applies the IPCONFIG.DAT ip/netmask/gw passed as module
//arguments, so no separate setconfig RPC is needed.
static int smbLoadModules(void)
{
	int ID, ret;

	//If the UDPFS stack is active this resets the IOP first, so the
	//ps2ip+smbman stack can bind SMAP exclusively.
	smbPrepareNetworkStack();

	ensureCoreIoStackReady();
	setupPowerOff();
	getIpConfig();

	if (!ensurePs2Dev9Loaded()) {
		snprintf(smb_status_msg, sizeof(smb_status_msg),
		         "SMB: DEV9初始化失败");
		return 0;
	}

	if (!have_smb_ps2ip) {
		ID = SifExecModuleBuffer(smb_ps2ip_irx, size_smb_ps2ip_irx, 0, NULL, &ret);
		DPRINTF(" [SMB_PS2IP]: ID=%d, ret=%d\n", ID, ret);
		have_smb_ps2ip = (ID >= 0 && ret >= 0);
		if (!have_smb_ps2ip) {
			snprintf(smb_status_msg, sizeof(smb_status_msg),
			         "SMB: ps2ip模块加载失败");
			return 0;
		}
	}

	if (!have_smb_smap) {
		ID = SifExecModuleBuffer(smb_smap_irx, size_smb_smap_irx,
		                         if_conf_len, &if_conf[0], &ret);
		DPRINTF(" [SMB_SMAP]: ID=%d, ret=%d\n", ID, ret);
		have_smb_smap = (ID >= 0 && ret >= 0);
		if (!have_smb_smap) {
			snprintf(smb_status_msg, sizeof(smb_status_msg),
			         "SMB: SMAP模块加载失败");
			return 0;
		}
	}

	if (!have_smb_ps2ips) {
		ID = SifExecModuleBuffer(smb_ps2ips_irx, size_smb_ps2ips_irx, 0, NULL, &ret);
		DPRINTF(" [SMB_PS2IPS]: ID=%d, ret=%d\n", ID, ret);
		have_smb_ps2ips = (ID >= 0 && ret >= 0);
	}

	if (!have_smbman) {
		ID = SifExecModuleBuffer(smbman_irx, size_smbman_irx, 0, NULL, &ret);
		DPRINTF(" [SMBMAN]: ID=%d, ret=%d\n", ID, ret);
		have_smbman = (ID >= 0 && ret >= 0);
		if (!have_smbman) {
			snprintf(smb_status_msg, sizeof(smb_status_msg),
			         "SMB: smbman模块加载失败");
			return 0;
		}
	}
	return 1;
}
//------------------------------
//endfunc smbLoadModules
//--------------------------------------------------------------
void smbResetState(void)
{
	have_smb_ps2ip = 0;
	have_smb_smap = 0;
	have_smb_ps2ips = 0;
	have_smbman = 0;
	smb_state = SMB_STATE_IDLE;
	smb_ready = 0;
	smb_status_msg[0] = '\0';
}
//------------------------------
//endfunc smbResetState
//--------------------------------------------------------------
void smbDisconnect(void)
{
	if (have_smbman) {
		fileXioDevctl("smb:", SMB_DEVCTL_CLOSESHARE, NULL, 0, NULL, 0);
		fileXioDevctl("smb:", SMB_DEVCTL_LOGOFF, NULL, 0, NULL, 0);
	}
	smb_state = SMB_STATE_IDLE;
	smb_ready = 0;
	memset(smb_password, 0, sizeof(smb_password));
}
//------------------------------
//endfunc smbDisconnect
//--------------------------------------------------------------
//Log on to the server and open the share. On success the "smb:"
//iomanX device is live. Status text lands in smb_status_msg.
int smbConnect(void)
{
	smbLogOn_in_t logon;
	smbOpenShare_in_t openShare;
	smbGetPasswordHashes_in_t hashInput;
	smbGetPasswordHashes_out_t hashes;
	int result;

	if (smb_state == SMB_STATE_CONNECTED)
		return 1;

	smb_status_msg[0] = '\0';

	result = smbLoadConfig();
	if (result == 0) {
		snprintf(smb_status_msg, sizeof(smb_status_msg),
		         "未找到SMB.CNF (mc?:/SYS-CONF/)");
		return 0;
	}
	if (result < 0) {
		snprintf(smb_status_msg, sizeof(smb_status_msg),
		         "SMB.CNF无效 (服务器IP/共享名错误)");
		return 0;
	}

	if (!smbLoadModules())
		return 0;

	memset(&logon, 0, sizeof(logon));
	memset(&openShare, 0, sizeof(openShare));
	strncpy(logon.serverIP, smb_server_ip, sizeof(logon.serverIP) - 1);
	logon.serverPort = smb_server_port;
	strncpy(logon.User, smb_user, sizeof(logon.User) - 1);
	strncpy(openShare.ShareName, smb_share, sizeof(openShare.ShareName) - 1);

	if (smb_password_type == HASHED_PASSWORD) {
		memset(&hashInput, 0, sizeof(hashInput));
		memset(&hashes, 0, sizeof(hashes));
		strncpy(hashInput.password, smb_password, sizeof(hashInput.password) - 1);
		result = fileXioDevctl("smb:", SMB_DEVCTL_GETPASSWORDHASHES,
		                       &hashInput, sizeof(hashInput),
		                       &hashes, sizeof(hashes));
		if (result < 0) {
			snprintf(smb_status_msg, sizeof(smb_status_msg),
			         "SMB: 密码哈希失败 (%d)", result);
			return 0;
		}
		memcpy(logon.Password, &hashes, sizeof(hashes));
		memcpy(openShare.Password, &hashes, sizeof(hashes));
	} else if (smb_password_type == PLAINTEXT_PASSWORD) {
		strncpy(logon.Password, smb_password, sizeof(logon.Password) - 1);
		strncpy(openShare.Password, smb_password, sizeof(openShare.Password) - 1);
	}
	logon.PasswordType = smb_password_type;
	openShare.PasswordType = smb_password_type;

	result = fileXioDevctl("smb:", SMB_DEVCTL_LOGON,
	                       &logon, sizeof(logon), NULL, 0);
	if (result < 0) {
		if (result == -SMB_DEVCTL_LOGON_ERR_CONN)
			snprintf(smb_status_msg, sizeof(smb_status_msg),
			         "SMB: 无法连接服务器 %s", smb_server_ip);
		else if (result == -SMB_DEVCTL_LOGON_ERR_PROT)
			snprintf(smb_status_msg, sizeof(smb_status_msg),
			         "SMB: 服务器协议错误");
		else
			snprintf(smb_status_msg, sizeof(smb_status_msg),
			         "SMB: 登录失败 (%d)", result);
		return 0;
	}

	result = fileXioDevctl("smb:", SMB_DEVCTL_OPENSHARE,
	                       &openShare, sizeof(openShare), NULL, 0);
	if (result < 0) {
		fileXioDevctl("smb:", SMB_DEVCTL_LOGOFF, NULL, 0, NULL, 0);
		snprintf(smb_status_msg, sizeof(smb_status_msg),
		         "SMB: 无法打开共享 '%s' (%d)", smb_share, result);
		return 0;
	}

	memset(smb_password, 0, sizeof(smb_password));
	memset(&hashInput, 0, sizeof(hashInput));
	memset(&hashes, 0, sizeof(hashes));
	memset(logon.Password, 0, sizeof(logon.Password));
	memset(openShare.Password, 0, sizeof(openShare.Password));

	smb_state = SMB_STATE_CONNECTED;
	smb_ready = 1;
	return 1;
}
//------------------------------
//endfunc smbConnect
//--------------------------------------------------------------
//End of file: smb.c
//--------------------------------------------------------------