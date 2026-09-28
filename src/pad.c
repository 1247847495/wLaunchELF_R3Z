//---------------------------------------------------------------------------
// File name:   pad.c
//---------------------------------------------------------------------------
#include "launchelf.h"

static char padBuf_t[2][256] __attribute__((aligned(64)));
struct padButtonStatus buttons_t[2];
u32 padtype_t[2];
u32 paddata, paddata_t[2];
u32 old_pad = 0, old_pad_t[2] = {0, 0};
u32 new_pad, new_pad_t[2];
u32 joy_value = 0;
static int test_joy = 0;
//IOP高负载(如进入APA硬盘)会使padman采样出现"按键数据↔全零"交替的异常;
//交替的全零段若被确认为松开,按键"重现"会被当作全新按下(无首按延迟),
//导致"按一下变两三下"。这里用时间窗口消抖:全零持续不足120ms一律视为
//采样毛刺不确认松开,old状态保持,重现时仍走repeat限速,不产生额外事件。
//zero_since[nr]_t[port]记录首个全零帧的时刻(0=当前非全零)。
#define PAD_RELEASE_CONFIRM_MS 120  //全零持续超过该时长才确认松开
static u64 zero_since_t[2] = {0, 0};
static u64 zero_since_nr_t[2] = {0, 0};
//假松开→重现保护:IOP长卡顿(全零超120ms)会被确认为松开,卡顿结束后按键
//"重现"又多出一次事件。这里记录确认松开的时刻与组合,100ms内重现的按键
//包含松开前的完整组合(允许附加数据抖动位)即视为假松开的延续:恢复状态
//但不出事件。真实双击同键的间隔(人手完全松开再按下)远大于100ms,不受影响。
#define PAD_REAPPEAR_GUARD_MS 100
static u64 rel_time_t[2] = {0, 0}, rel_time_nr_t[2] = {0, 0};
static u32 rel_pad_t[2] = {0, 0}, rel_pad_nr_t[2] = {0, 0};
//事件级限速:每个手柄口两次按钮事件至少间隔40ms(与按住连发同速25Hz)。
//padRead偶发失败会造成旧事件值被重复消费,采样异常会造成"假松开→重现"
//事件风暴;无论输入数据怎么异常,事件率被钳制在正常连发速度以内,
//移动/按键永远不会异常加速。
static u64 evt_min_time[2] = {0, 0};

void clearPadPressState(void)
{
	memset(old_pad_t, 0, sizeof(old_pad_t));
	memset(new_pad_t, 0, sizeof(new_pad_t));
	memset(paddata_t, 0, sizeof(paddata_t));
	old_pad = 0;
	new_pad = 0;
	paddata = 0;
	joy_value = 0;
	memset(zero_since_t, 0, sizeof(zero_since_t));
	memset(zero_since_nr_t, 0, sizeof(zero_since_nr_t));
	memset(rel_time_t, 0, sizeof(rel_time_t));
	memset(rel_time_nr_t, 0, sizeof(rel_time_nr_t));
	memset(rel_pad_t, 0, sizeof(rel_pad_t));
	memset(rel_pad_nr_t, 0, sizeof(rel_pad_nr_t));
}

//---------------------------------------------------------------------------
// read PAD, without KB, and allow no auto-repeat. This is needed in code
// that is used regardless of VSync cycles, and where KB is not wanted.
//---------------------------------------------------------------------------
int readpad_noKBnoRepeat(void)
{
	int port, state, ret[2] = {0, 0};

	for (port = 0; port < 2; port++) {
		if ((state = padGetState(port, 0)) == PAD_STATE_STABLE || (state == PAD_STATE_FINDCTP1)) {
			//Deal with cases where pad state is valid for padRead
			ret[port] = padRead(port, 0, &buttons_t[port]);
			if (ret[port] != 0) {
				paddata_t[port] = 0xffff ^ buttons_t[port].btns;
				if (old_pad_t[port] != 0 && paddata_t[port] == 0) {
					//从按住到全零:短窗口内视为采样毛刺,不确认松开
					if (zero_since_nr_t[port] == 0)
						zero_since_nr_t[port] = Timer();
					if (Timer() - zero_since_nr_t[port] < PAD_RELEASE_CONFIRM_MS) {
						new_pad_t[port] = 0;  //保持old_pad_t,按键重现时不产生额外事件
					} else {
						rel_pad_nr_t[port] = old_pad_t[port];  //确认松开,记录组合
						rel_time_nr_t[port] = Timer();
						old_pad_t[port] = 0;
						zero_since_nr_t[port] = 0;
						new_pad_t[port] = 0;
					}
				} else {
					zero_since_nr_t[port] = 0;  //数据非零:清全零计时
					if (old_pad_t[port] == 0 && paddata_t[port] != 0 &&
					    (paddata_t[port] & rel_pad_nr_t[port]) == rel_pad_nr_t[port] &&
					    Timer() - rel_time_nr_t[port] < PAD_REAPPEAR_GUARD_MS) {
						//松开后100ms内重现(含)松开前的组合:IOP卡顿假松开,恢复状态不出事件
						old_pad_t[port] = paddata_t[port];
						new_pad_t[port] = 0;
					} else {
						new_pad_t[port] = paddata_t[port] & ~old_pad_t[port];
						old_pad_t[port] = paddata_t[port];
					}
				}
			} else {
				//padRead失败:清掉旧事件值,防止被主循环当作新事件重复消费
				new_pad_t[port] = 0;
			}
		} else {
			//Deal with cases where pad state is not valid for padRead
			new_pad_t[port] = 0;
		}                                   //ends 'if' testing for state valid for padRead
		//事件级限速:距上次事件不足40ms的事件丢弃(毛刺风暴防护)
		if (new_pad_t[port] != 0) {
			if (Timer() < evt_min_time[port])
				new_pad_t[port] = 0;
			else
				evt_min_time[port] = Timer() + 40;
		}
	}                                       //ends for
	new_pad = new_pad_t[0] | new_pad_t[1];  //This has only new button bits
	paddata = paddata_t[0] | paddata_t[1];  //This has all pressed button bits
	return (ret[0] | ret[1]);
}
//------------------------------
//endfunc readpad_noKBnoRepeat
//---------------------------------------------------------------------------
// read PAD, but ignore KB. This is needed in code with own KB handlers,
// such as the virtual keyboard input routines for 'Rename' and 'New Dir'
//---------------------------------------------------------------------------
int readpad_no_KB(void)
{
	static u64 rpt_time[2] = {0, 0};
	static int rpt_count[2];
	int port, state, ret[2] = {0, 0};

	for (port = 0; port < 2; port++) {
		if ((state = padGetState(port, 0)) == PAD_STATE_STABLE || (state == PAD_STATE_FINDCTP1)) {
			//Deal with cases where pad state is valid for padRead
			ret[port] = padRead(port, 0, &buttons_t[port]);
			if (ret[port] != 0) {
				paddata_t[port] = 0xffff ^ buttons_t[port].btns;
				if ((padtype_t[port] == 2) && (1 & (test_joy++))) {  //DualShock && time for joy scan
					joy_value = 0;
					if (buttons_t[port].rjoy_h >= 0xbf) {
						paddata_t[port] = PAD_R3_H1;
						joy_value = buttons_t[port].rjoy_h - 0xbf;
					} else if (buttons_t[port].rjoy_h <= 0x40) {
						paddata_t[port] = PAD_R3_H0;
						joy_value = -(buttons_t[port].rjoy_h - 0x40);
					} else if (buttons_t[port].rjoy_v <= 0x40) {
						paddata_t[port] = PAD_R3_V0;
						joy_value = -(buttons_t[port].rjoy_v - 0x40);
					} else if (buttons_t[port].rjoy_v >= 0xbf) {
						paddata_t[port] = PAD_R3_V1;
						joy_value = buttons_t[port].rjoy_v - 0xbf;
					} else if (buttons_t[port].ljoy_h >= 0xbf) {
						paddata_t[port] = PAD_L3_H1;
						joy_value = buttons_t[port].ljoy_h - 0xbf;
					} else if (buttons_t[port].ljoy_h <= 0x40) {
						paddata_t[port] = PAD_L3_H0;
						joy_value = -(buttons_t[port].ljoy_h - 0x40);
					} else if (buttons_t[port].ljoy_v <= 0x40) {
						paddata_t[port] = PAD_L3_V0;
						joy_value = -(buttons_t[port].ljoy_v - 0x40);
					} else if (buttons_t[port].ljoy_v >= 0xbf) {
						paddata_t[port] = PAD_L3_V1;
						joy_value = buttons_t[port].ljoy_v - 0xbf;
					}
				}
				new_pad_t[port] = paddata_t[port] & ~old_pad_t[port];
				if (old_pad_t[port] == paddata_t[port]) {
					//no change of pad data
					if (Timer() > rpt_time[port]) {
						new_pad_t[port] = paddata_t[port];  //Accept repeated buttons as new
						rpt_time[port] = Timer() + 40;      //Min delay = 40ms => 25Hz repeat
						if (rpt_count[port]++ < 20)
							rpt_time[port] += 43;  //Early delays = 83ms => 12Hz repeat
					}
				} else {
					//pad data has changed !
					if (old_pad_t[port] != 0 && paddata_t[port] == 0) {
						//从按住到全零:短窗口内视为采样毛刺/IOP卡顿,不确认松开
						if (zero_since_t[port] == 0)
							zero_since_t[port] = Timer();
						if (Timer() - zero_since_t[port] < PAD_RELEASE_CONFIRM_MS) {
							//保持old_pad_t不变,让按键"重现"时仍走repeat限速分支
							new_pad_t[port] = 0;
						} else {
							rpt_count[port] = 0;  //确认松开
							rpt_time[port] = Timer() + 400;
							rel_pad_t[port] = old_pad_t[port];  //记录松开前的组合
							rel_time_t[port] = Timer();
							old_pad_t[port] = 0;
							zero_since_t[port] = 0;
						}
					} else {
						if (old_pad_t[port] == 0 && paddata_t[port] != 0 &&
						    (paddata_t[port] & rel_pad_t[port]) == rel_pad_t[port] &&
						    Timer() - rel_time_t[port] < PAD_REAPPEAR_GUARD_MS) {
							//松开后100ms内重现(含)松开前的组合:IOP卡顿假松开,
							//恢复状态不出事件,repeat从现在重新起算
							rpt_count[port] = 0;
							rpt_time[port] = Timer() + 400;
							old_pad_t[port] = paddata_t[port];
							new_pad_t[port] = 0;
						} else {
							rpt_count[port] = 0;
							rpt_time[port] = Timer() + 400;  //Init delay = 400ms
							old_pad_t[port] = paddata_t[port];
						}
						zero_since_t[port] = 0;
					}
				}
			} else {
				//padRead失败:清掉旧事件值,防止被主循环当作新事件重复消费
				new_pad_t[port] = 0;
			}
		} else {
			//Deal with cases where pad state is not valid for padRead
			//NB: This should NOT clear KB repeat test variables
			new_pad_t[port] = 0;
			//old_pad_t[port]=0; //Clearing this could cause hasty repeats
		}  //ends 'if' testing for state valid for padRead
		//事件级限速:距上次事件不足40ms的事件丢弃(毛刺风暴防护);
		//正常连发间隔恰为40ms,不受影响
		if (new_pad_t[port] != 0) {
			if (Timer() < evt_min_time[port])
				new_pad_t[port] = 0;
			else
				evt_min_time[port] = Timer() + 40;
		}
	}      //ends for
	new_pad = new_pad_t[0] | new_pad_t[1];
	paddata = paddata_t[0] | paddata_t[1];  //This has all pressed button bits
	return (ret[0] | ret[1]);
}
//------------------------------
//endfunc readpad_no_KB
//---------------------------------------------------------------------------
// simPadKB attempts reading data from a USB keyboard, and map this as a
// virtual gamepad. (Very improvised and sloppy, but it should work fine.)
//IOP拥塞时ps2kbd驱动会返回脏扫描码,假键盘事件与手柄真实按键叠加会
//造成"按一下变多下",因此键盘事件必须经过与手柄一致的防护:
//1.与手柄当前按住的键重叠的事件直接丢弃(手柄状态机已在管理该键)
//2.与手柄事件共享40ms限速桶(手柄事件后40ms内的键盘事件丢弃)
//3.同键120ms节流(键盘噪声常为同码连发;真实键盘按住auto-repeat约
//  92ms间隔,节流后约8Hz,菜单导航仍可正常移动)
//---------------------------------------------------------------------------
int simPadKB(void)
{
	static u64 kb_evt_time = 0;
	static u32 kb_last_pad = 0;
	int ret, command;
	char KeyPress;

	if (!setting->usbkbd_used || !ensureUsbKeyboardReady() || !PS2KbdRead(&KeyPress))
		return 0;
	if (KeyPress != PS2KBD_ESCAPE_KEY)
		command = KeyPress;
	else {
		PS2KbdRead(&KeyPress);
		command = 0x100 + KeyPress;
	}
	ret = 1;  //Assume that the entered key is a valid command
	switch (command) {
		case 0x11B:  //Escape == Triangle
			new_pad = PAD_TRIANGLE;
			break;
		case 0x00A:  //Enter == OK
			if (!swapKeys)
				new_pad = PAD_CIRCLE;
			else
				new_pad = PAD_CROSS;
			break;
		case 0x020:  //Space == Cancel/Mark
			if (!swapKeys)
				new_pad = PAD_CROSS;
			else
				new_pad = PAD_CIRCLE;
			break;
		case 0x031:  //'1' == L1
			new_pad = PAD_L1;
			break;
		case 0x032:  //'2' == L2
			new_pad = PAD_L2;
			break;
		case 0x033:  //'3' == L3
			new_pad = PAD_L3;
			break;
		case 0x077:  //'w' == Up
			new_pad = PAD_UP;
			break;
		case 0x061:  //'a' == Left
			new_pad = PAD_LEFT;
			break;
		case 0x073:  //'s' == Right
			new_pad = PAD_RIGHT;
			break;
		case 0x07A:  //'z' == Down
			new_pad = PAD_DOWN;
			break;
		case 0x030:  //'0' == R1
			new_pad = PAD_R1;
			break;
		case 0x039:  //'9' == R2
			new_pad = PAD_R2;
			break;
		case 0x038:  //'8' == R3
			new_pad = PAD_R3;
			break;
		case 0x069:  //'i' == Triangle
			new_pad = PAD_TRIANGLE;
			break;
		case 0x06A:  //'j' == Square
			new_pad = PAD_SQUARE;
			break;
		case 0x06B:  //'k' == Circle
			new_pad = PAD_CIRCLE;
			break;
		case 0x06D:  //'m' == Cross
			new_pad = PAD_CROSS;
			break;
		case 0x101:  //F1 == L1
			new_pad = PAD_L1;
			break;
		case 0x102:  //F2 == L2
			new_pad = PAD_L2;
			break;
		case 0x103:  //F3 == L3
			new_pad = PAD_L3;
			break;
		case 0x12C:  //Up == Up
			new_pad = PAD_UP;
			break;
		case 0x12A:  //Left == Left
			new_pad = PAD_LEFT;
			break;
		case 0x129:  //Right == Right
			new_pad = PAD_RIGHT;
			break;
		case 0x12B:  //Down == Down
			new_pad = PAD_DOWN;
			break;
		case 0x123:  //Insert == Select
			new_pad = PAD_SELECT;
			break;
		case 0x10C:  //F12 == R1
			new_pad = PAD_R1;
			break;
		case 0x10B:  //F11 == R2
			new_pad = PAD_R2;
			break;
		case 0x10A:  //F10 == R3
			new_pad = PAD_R3;
			break;
		case 0x124:  //Home == Triangle
			new_pad = PAD_TRIANGLE;
			break;
		case 0x127:  //End == Square
			new_pad = PAD_SQUARE;
			break;
		case 0x125:  //PgUp == Circle
			new_pad = PAD_CIRCLE;
			break;
		case 0x128:  //PgDn == Cross
			new_pad = PAD_CROSS;
			break;
		case 0x126:  //Delete == Start
			new_pad = PAD_START;
			break;
		default:  //Unrecognized key => no pad button
			ret = 0;
			break;
	}
	if (ret && new_pad) {
		u64 now = Timer();
		//防护1:手柄已按住的键,键盘再报同键是重复噪声,直接丢弃
		if (new_pad & paddata_t[0] || new_pad & paddata_t[1]) {
			new_pad = 0;
			return 0;
		}
		//防护2:与手柄事件同桶限速
		if (now < evt_min_time[0] || now < evt_min_time[1]) {
			new_pad = 0;
			return 0;
		}
		//防护3:同键120ms节流
		if (new_pad == kb_last_pad && now - kb_evt_time < 120) {
			new_pad = 0;
			return 0;
		}
		kb_evt_time = now;
		kb_last_pad = new_pad;
		//键盘事件也推高手柄限速桶,阻止"手柄事件+键盘噪声"叠加
		evt_min_time[0] = now + 40;
		evt_min_time[1] = now + 40;
	}
	return ret;
}
//------------------------------
//endfunc simPadKB
//---------------------------------------------------------------------------
// readpad will call readpad_no_KB, and if no new pad buttons are found, it
// will also attempt reading data from a USB keyboard, and map this as a
// virtual gamepad. (Very improvised and sloppy, but it should work fine.)
//---------------------------------------------------------------------------
int readpad(void)
{
	int ret;

	if ((ret = readpad_no_KB()) && new_pad)
		return ret;

	return simPadKB();
}
//------------------------------
//endfunc readpad
//---------------------------------------------------------------------------
// readpad_noRepeat calls readpad_noKBnoRepeat, and if no new pad buttons are
// found, it also attempts reading data from a USB keyboard, and map this as
// a virtual gamepad. (Very improvised and sloppy, but it should work fine.)
//---------------------------------------------------------------------------
int readpad_noRepeat(void)
{
	int ret;

	if ((ret = readpad_noKBnoRepeat()) && new_pad)
		return ret;

	return simPadKB();
}
//------------------------------
//endfunc readpad_noRepeat
//---------------------------------------------------------------------------
// Wait for specific PAD, but also accept disconnected state
void waitPadReady(int port, int slot)
{
	int state, lastState;
	char stateString[16];

	state = padGetState(port, slot);
	lastState = -1;
	while ((state != PAD_STATE_DISCONN) && (state != PAD_STATE_STABLE) && (state != PAD_STATE_FINDCTP1)) {
		if (state != lastState)
			padStateInt2String(state, stateString);
		lastState = state;
		state = padGetState(port, slot);
	}
}
//---------------------------------------------------------------------------
// Wait for any PAD, but also accept disconnected states
void waitAnyPadReady(void)
{
	int state_1, state_2;

	state_1 = padGetState(0, 0);
	state_2 = padGetState(1, 0);
	while ((state_1 != PAD_STATE_DISCONN) && (state_2 != PAD_STATE_DISCONN) && (state_1 != PAD_STATE_STABLE) && (state_2 != PAD_STATE_STABLE) && (state_1 != PAD_STATE_FINDCTP1) && (state_2 != PAD_STATE_FINDCTP1)) {
		state_1 = padGetState(0, 0);
		state_2 = padGetState(1, 0);
	}
}
//---------------------------------------------------------------------------
// setup PAD
int setupPad(void)
{
	int ret, i, port, state, modes;

	padInit(0);

	for (port = 0; port < 2; port++) {
		padtype_t[port] = 0;  //Assume that we don't have a proper PS2 controller
		if ((ret = padPortOpen(port, 0, &padBuf_t[port][0])) == 0)
			return 0;
		waitPadReady(port, 0);
		state = padGetState(port, 0);
		if (state != PAD_STATE_DISCONN) {  //if anything connected to this port
			modes = padInfoMode(port, 0, PAD_MODETABLE, -1);
			if (modes != 0) {  //modes != 0, so it may be a dualshock type
				for (i = 0; i < modes; i++) {
					if (padInfoMode(port, 0, PAD_MODETABLE, i) == PAD_TYPE_DUALSHOCK) {
						padtype_t[port] = 2;  //flag normal PS2 controller
						break;
					}
				}                     //ends for (modes)
			} else {                  //modes == 0, so this is a digital controller
				padtype_t[port] = 1;  //flag digital controller
			}
			if (padtype_t[port] == 2)                                          //if DualShock
				padSetMainMode(port, 0, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);  //Set DualShock
			else                                                               //else
				padSetMainMode(port, 0, PAD_MMODE_DIGITAL, PAD_MMODE_UNLOCK);  //Set Digital
			waitPadReady(port, 0);                                             //Await completion
		} else {                                                               //Nothing is connected to this port
			padSetMainMode(port, 0, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);      //Fake DualShock
			waitPadReady(port, 0);                                             //Await completion
		}
	}  //ends for (port)
	return 1;
}
//---------------------------------------------------------------------------
// End of file: pad.c
//---------------------------------------------------------------------------
