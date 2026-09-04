//--------------------------------------------------------------
//File name:   draw_text.c
//--------------------------------------------------------------
#include "draw_private.h"

//The font file ELISA100.FNT is needed to display MC save titles in japanese
//and the arrays defined here are needed to find correct data in that file
const u16 font404[] = {
    0xA2AF, 11,
    0xA2C2, 8,
    0xA2D1, 11,
    0xA2EB, 7,
    0xA2FA, 4,
    0xA3A1, 15,
    0xA3BA, 7,
    0xA3DB, 6,
    0xA3FB, 4,
    0xA4F4, 11,
    0xA5F7, 8,
    0xA6B9, 8,
    0xA6D9, 38,
    0xA7C2, 15,
    0xA7F2, 13,
    0xA8C1, 720,
    0xCFD4, 43,
    0xF4A5, 1030,
    0, 0};

// ASCII��SJIS�̕ϊ��p�z��
static const u8 sjis_lookup_81[256] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0x00
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0x10
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0x20
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0x30
    ' ', ',', '.', ',', '.', 0xFF, ':', ';', '?', '!', 0xFF, 0xFF, '\xff', '`', 0xFF, '^',              // 0x40
    0xFF, '_', 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, '0', 0xFF, '-', '-', 0xFF, 0xFF,      // 0x50
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, '\'', '\'', '"', '"', '(', ')', 0xFF, 0xFF, '[', ']', '{',         // 0x60
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, '+', '-', 0xFF, '*', 0xFF,     // 0x70
    '/', '=', 0xFF, '<', '>', 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, '\xff', 0xFF, 0xFF, '\xff', 0xFF,        // 0x80
    '$', 0xFF, 0xFF, '%', '#', '&', '*', '@', 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,        // 0x90
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0xA0
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0xB0
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, '&', '|', '!', 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,     // 0xC0
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0xD0
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0xE0
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0xF0
};
static const u8 sjis_lookup_82[256] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0x00
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0x10
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0x20
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0x30
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, '0',   // 0x40
    '1', '2', '3', '4', '5', '6', '7', '8', '9', 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,           // 0x50
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',                  // 0x60
    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,            // 0x70
    0xFF, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o',                 // 0x80
    'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,             // 0x90
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0xA0
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0xB0
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0xC0
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0xD0
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0xE0
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0xF0
};
int loadFont(char *path_arg)
{
	int fd;

	if (strlen(path_arg) != 0) {
		char FntPath[MAX_PATH];
		genFixPath(path_arg, FntPath);
		fd = genOpen(FntPath, FIO_O_RDONLY);
		if (fd < 0) {
			genClose(fd);
			goto use_default;
		}  // end if failed open file
		genLseek(fd, 0, SEEK_SET);
		if (genLseek(fd, 0, SEEK_END) > 4700) {
			genClose(fd);
			goto use_default;
		}
		genLseek(fd, 0, SEEK_SET);
		u8 FontHeader[100];
		genRead(fd, FontHeader, 100);
		if ((FontHeader[0] == 0x00) &&
		    (FontHeader[1] == 0x02) &&
		    (FontHeader[70] == 0x60) &&
		    (FontHeader[72] == 0x60) &&
		    (FontHeader[83] == 0x90)) {
			genLseek(fd, 1018, SEEK_SET);
			memset(FontBuffer, 0, 32 * 16);
			genRead(fd, FontBuffer + 32 * 16, 224 * 16);  //.fnt files skip 1st 32 chars
			genClose(fd);
			return 1;
		} else {  // end if good fnt file
			genClose(fd);
			goto use_default;
		}     // end else bad fnt file
	} else {  // end if external font file
	use_default:
		memcpy(FontBuffer, &font_uLE, 4096);
	}  // end else build-in font
	return 0;
}
void drawChar(unsigned int c, int x, int y, u64 colour)
{
	int i, j, pixBase, pixMask;
	u8 *cm;

	updateScr_1 = 1;

	if (c >= FONT_COUNT)
		c = '_';
	if (c > 0xFF)                  //if char is beyond normal ascii range
		cm = &font_uLE[c * 16];    //  cm points to special char def in default font
	else                           //else char is inside normal ascii range
		cm = &FontBuffer[c * 16];  //  cm points to normal char def in active font

	pixMask = 0x80;
	for (i = 0; i < 8; i++) {  //for i == each pixel column
		pixBase = -1;
		for (j = 0; j < 16; j++) {                     //for j == each pixel row
			if ((pixBase < 0) && (cm[j] & pixMask)) {  //if start of sequence
				pixBase = j;
			} else if ((pixBase > -1) && !(cm[j] & pixMask)) {  //if end of sequence
				gsKit_prim_sprite(gsGlobal, x + i, y + pixBase - 1, x + i + 1, y + j - 1, 1, colour);
				pixBase = -1;
			}
		}                  //ends for j == each pixel row
		if (pixBase > -1)  //if end of sequence including final row
			gsKit_prim_sprite(gsGlobal, x + i, y + pixBase - 1, x + i + 1, y + j - 1, 1, colour);
		pixMask >>= 1;
	}  //ends for i == each pixel column
}
void drawChar2(int n, int x, int y, u64 colour)
{
	unsigned int i, j;
	u8 b;

	updateScr_1 = 1;

	for (i = 0; i < 8; i++) {
		b = elisaFnt[n + i];
		for (j = 0; j < 8; j++) {
			if (b & 0x80) {
				gsKit_prim_sprite(gsGlobal, x + j, y + i * 2 - 2, x + j + 1, y + i * 2, 1, colour);
			}
			b = b << 1;
		}
	}
}
static void drawCharCn(const u8 *glyph, int x, int y, u64 colour)
{
	int row, col;

	updateScr_1 = 1;

	for (row = 0; row < 16; row++) {
		u16 bits = (glyph[row * 2] << 8) | glyph[row * 2 + 1];
		int runStart = -1;
		for (col = 0; col <= 16; col++) {
			int on = (col < 16) && (bits & (0x8000 >> col));
			if (on && runStart < 0)
				runStart = col;
			else if (!on && runStart >= 0) {
				gsKit_prim_sprite(gsGlobal, x + runStart, y + row, x + col, y + row + 1, 1, colour);
				runStart = -1;
			}
		}
	}
}
//无字形时的占位框（"□"），避免缺字显示成乱码字节
static const u8 glyph_box[32] = {
    0xFF, 0xFF, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01,
    0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0xFF, 0xFF};

//解码 s 处的一个 UTF-8 字符（2 或 3 字节序列）
//返回字形位图指针；*adv=消耗字节数, *width=显示宽度, *is_cjk=按中文处理
//返回 NULL 时 *adv=1/*width=8，调用方按普通单字节字符处理
static const u8 *utf8_next(const char *s, int *adv, int *width, int *is_cjk)
{
	unsigned int c1 = (unsigned char)s[0];
	unsigned int code;
	const u8 *g;

	*adv = 1;
	*width = 8;
	*is_cjk = 0;
	if ((c1 & 0xE0) == 0xC0) {  //2 字节序列 (U+0080..U+07FF)
		unsigned int c2 = (unsigned char)s[1];
		if ((c2 & 0xC0) == 0x80) {
			code = ((c1 & 0x1F) << 6) | (c2 & 0x3F);
			g = font_cn_lookup(code);
			if (g != NULL) {
				*adv = 2;
				*width = 16;
				*is_cjk = 1;
				return g;
			}
		}
		return NULL;
	}
	if ((c1 & 0xF0) == 0xE0) {  //3 字节序列 (U+0800..U+FFFF)
		unsigned int u2 = (unsigned char)s[1];
		unsigned int u3 = (unsigned char)s[2];
		if (u2 && u3 && ((u2 & 0xC0) == 0x80) && ((u3 & 0xC0) == 0x80)) {
			code = ((c1 & 0x0F) << 12) | ((u2 & 0x3F) << 6) | (u3 & 0x3F);
			g = font_cn_lookup(code);
			if (g != NULL) {
				*adv = 3;
				*width = 16;
				*is_cjk = 1;
				return g;
			}
			if (code >= 0x2E80) {  //CJK 区缺字形 → 占位框
				*adv = 3;
				*width = 16;
				*is_cjk = 1;
				return glyph_box;
			}
		}
	}
	return NULL;
}
//按显示宽度截断 UTF-8 字符串（在字符边界切割，不会产生乱码字节）
//中文字符宽 16px、ASCII 宽 8px；截断时末尾以 '~' 标记，max_width 需含 '~' 的宽度
void utf8_truncate_width(char *s, int max_width)
{
	int i = 0, w = 0;

	while (s[i] != 0) {
		int adv, cw, is_cjk;

		utf8_next(s + i, &adv, &cw, &is_cjk);  //无字形时 adv=1/cw=8
		if (w + cw > max_width - 8) {          //为 '~' 预留 8px
			s[i] = '~';
			s[i + 1] = 0;
			return;
		}
		w += cw;
		i += adv;
	}
}

//检测并修复"GBK 伪装 UTF-16"的文件名：旧工具把 GBK 字节当 UTF-16 写入 LFN，
//FatFs(UTF-8 模式)读出后显示为 ÖÐ 之类的 Latin-1 串。
//两遍法：第一遍统计 GB2312 配对成功率；成功率≥75%（且至少2对）才启用转换，
//防止把法语/西语等含 Latin-1 字母的正常文件名误转成汉字。
//第二遍转换：配对成功→汉字；损坏对/落单字节→原样保留（部分修复优于整串放弃）。
//真 UTF-8 中文（3 字节序列）直接原样返回。dst 可与 src 同缓冲区。
int gbk_fake_to_utf8(char *dst, const char *src)
{
	int i, di = 0;
	int pending = 0;   //0=无挂起的 GBK 高字节, 否则为高字节值
	int pairs_ok = 0;  //配对且查表成功的对数
	int pairs_bad = 0; //落单/查表失败的对数

	//第一遍：统计可转换性
	i = 0;
	while (src[i] != 0) {
		unsigned int c = (unsigned char)src[i];

		if (c < 0x80) {  //ASCII
			if (pending) { pairs_bad++; pending = 0; }  //高字节被 ASCII 打断
			i++;
		} else if ((c & 0xE0) == 0xC0) {  //2 字节 UTF-8 → 码点 0x80..0x7FF
			unsigned int c2 = (unsigned char)src[i + 1];
			unsigned int cp;

			if ((c2 & 0xC0) != 0x80)
				goto keep;  //坏 UTF-8 → 不是 GBK 伪装
			cp = ((c & 0x1F) << 6) | (c2 & 0x3F);
			i += 2;
			if (cp >= 0xA1 && cp <= 0xFE) {  //GBK 字节候选
				if (pending == 0) {
					pending = cp;
				} else {
					if (gbk_lookup_uni((pending << 8) | cp) != 0)
						pairs_ok++;
					else
						pairs_bad++;
					pending = 0;
				}
			} else {
				goto keep;  //0x80-0xA0 范围 Latin-1 字符混入 → 放弃
			}
		} else if ((c & 0xF0) == 0xE0) {  //3 字节 UTF-8 → 真 CJK，无需修复
			goto keep;
		} else {
			goto keep;  //4 字节序列或其他 → 放弃
		}
	}
	if (pending)
		pairs_bad++;  //结尾落单高字节

	//决策阈值：至少 2 对成功，且失败不超过成功的 1/3（约 75% 成功率）
	if (pairs_ok < 2 || pairs_bad * 3 > pairs_ok)
		goto keep;

	//第二遍：转换输出（失败部分原样保留）
	i = 0;
	pending = 0;
	while (src[i] != 0) {
		unsigned int c = (unsigned char)src[i];

		if (c < 0x80) {
			if (pending) {  //落单高字节 → 原样输出其 2 字节 UTF-8
				dst[di++] = (char)(0xC0 | (pending >> 6));
				dst[di++] = (char)(0x80 | (pending & 0x3F));
				pending = 0;
			}
			dst[di++] = (char)c;
			i++;
		} else {  //此处必为 A1-FE 候选（第一遍已验证）
			unsigned int cp;

			i += 2;  //跳过 2 字节 UTF-8 序列
			cp = ((c & 0x1F) << 6) | ((unsigned char)src[i - 1] & 0x3F);
			if (pending == 0) {
				pending = cp;
			} else {
				u16 uni = gbk_lookup_uni((pending << 8) | cp);
				if (uni != 0) {  //成功 → 汉字 UTF-8
					dst[di++] = (char)(0xE0 | (uni >> 12));
					dst[di++] = (char)(0x80 | ((uni >> 6) & 0x3F));
					dst[di++] = (char)(0x80 | (uni & 0x3F));
				} else {  //查表失败 → 原样输出这对候选的 4 字节
					dst[di++] = (char)(0xC0 | (pending >> 6));
					dst[di++] = (char)(0x80 | (pending & 0x3F));
					dst[di++] = (char)(0xC0 | (cp >> 6));
					dst[di++] = (char)(0x80 | (cp & 0x3F));
				}
				pending = 0;
			}
		}
	}
	if (pending) {  //结尾落单
		dst[di++] = (char)(0xC0 | (pending >> 6));
		dst[di++] = (char)(0x80 | (pending & 0x3F));
	}
	dst[di] = 0;
	return 1;

keep:
	if (dst != src)
		strcpy(dst, src);
	return 0;
}
static int text_display_width(const char *s, int spacing)
{
	unsigned int c1, c2;
	int i = 0, w = 0;

	while ((c1 = (unsigned char)s[i]) != 0) {
		int adv, cw, is_cjk;

		if (utf8_next(s + i, &adv, &cw, &is_cjk) != NULL) {  //中文字符(或占位框)
			w += cw;
			i += adv;
			continue;
		}
		i++;  //消耗单字节字符
		if (c1 != 0xFF) {  // Normal character
			w += spacing;
			continue;
		}
		// Special character sequence starting with 0xFF ('\xff')
		if ((c2 = (unsigned char)s[i++]) == 0)
			break;
		if ((c2 < '0') || (c2 > '='))
			continue;
		w += 16;
	}
	return w;
}
int printXY(const char *s, int x, int y, u64 colour, int draw, int space)
{
	unsigned int c1, c2;
	int i;
	int text_spacing = 8;

	if (space > 0) {
		while (text_display_width(s, text_spacing) > space)
			if (--text_spacing <= 5)
				break;
	} else {
		while (text_display_width(s, text_spacing) > SCREEN_WIDTH - SCREEN_MARGIN - FONT_WIDTH * 2)
			if (--text_spacing <= 5)
				break;
	}

	i = 0;
	while ((c1 = (unsigned char)s[i]) != 0) {
		int adv, cw, is_cjk;
		const u8 *glyph = utf8_next(s + i, &adv, &cw, &is_cjk);

		if (glyph != NULL) {  //中文字符(或占位框)
			if (draw)
				drawCharCn(glyph, x, y, colour);
			x += cw;
			i += adv;
			if (x > SCREEN_WIDTH - SCREEN_MARGIN - FONT_WIDTH)
				break;
			continue;
		}
		i++;  //消耗单字节字符
		if (c1 != 0xFF) {  // Normal character
			if (draw)
				drawChar(c1, x, y, colour);
			x += text_spacing;
			if (x > SCREEN_WIDTH - SCREEN_MARGIN - FONT_WIDTH)
				break;
			continue;
		}  //End if for normal character
		// Here we got a sequence starting with 0xFF ('\xff')
		if ((c2 = (unsigned char)s[i++]) == 0)
			break;
		if ((c2 < '0') || (c2 > '='))
			continue;
		c1 = (c2 - '0') * 2 + 0x100;
		if (draw) {
			//expand sequence �0=Circle  �1=Cross  �2=Square  �3=Triangle  �4=FilledBox
			//"\xff:"=Pad_Right  "\xff;"=Pad_Down  "\xff<"=Pad_Left  "\xff="=Pad_Up
			drawChar(c1, x, y, colour);
			x += 8;
			if (x > SCREEN_WIDTH - SCREEN_MARGIN - FONT_WIDTH)
				break;
			drawChar(c1 + 1, x, y, colour);
			x += 8;
			if (x > SCREEN_WIDTH - SCREEN_MARGIN - FONT_WIDTH)
				break;
		}
	}  // ends while(1)
	return x;
}
int printXY_sjis(const unsigned char *s, int x, int y, u64 colour, int draw)
{
	int n;
	u8 ascii;
	u16 code;
	int i, j, tmp;

	i = 0;
	while (s[i]) {
		if ((s[i] & 0x80) && s[i + 1]) {  //we have top bit and some more char ?
			// SJIS
			code = s[i++];
			code = (code << 8) + s[i++];

			switch (code) {
				// Circle == "��"
				case 0x819B:
					if (draw) {
						drawChar(0x100, x, y, colour);
						drawChar(0x101, x + 8, y, colour);
					}
					x += 16;
					break;
				// Cross == "\xff~"
				case 0x817E:
					if (draw) {
						drawChar(0x102, x, y, colour);
						drawChar(0x103, x + 8, y, colour);
					}
					x += 16;
					break;
				// Square == "��"
				case 0x81A0:
					if (draw) {
						drawChar(0x104, x, y, colour);
						drawChar(0x105, x + 8, y, colour);
					}
					x += 16;
					break;
				// Triangle == "��"
				case 0x81A2:
					if (draw) {
						drawChar(0x106, x, y, colour);
						drawChar(0x107, x + 8, y, colour);
					}
					x += 16;
					break;
				// FilledBox == "��"
				case 0x81A1:
					if (draw) {
						drawChar(0x108, x, y, colour);
						drawChar(0x109, x + 8, y, colour);
					}
					x += 16;
					break;
				default:
					if (elisaFnt != NULL) {  // elisa font is available ?
						tmp = y;
						if (code <= 0x829A)
							tmp++;
						// SJIS����EUC�ɕϊ�
						if (code >= 0xE000)
							code -= 0x4000;
						code = ((((code >> 8) & 0xFF) - 0x81) << 9) + (code & 0x00FF);
						if ((code & 0x00FF) >= 0x80)
							code--;
						if ((code & 0x00FF) >= 0x9E)
							code += 0x62;
						else
							code -= 0x40;
						code += 0x2121 + 0x8080;

						// EUC����b�����t�H���g�̔ԍ��𐶐�
						n = (((code >> 8) & 0xFF) - 0xA1) * (0xFF - 0xA1) + (code & 0xFF) - 0xA1;
						j = 0;
						while (font404[j]) {
							if (code >= font404[j]) {
								if (code <= font404[j] + font404[j + 1] - 1) {
									n = -1;
									break;
								} else {
									n -= font404[j + 1];
								}
							}
							j += 2;
						}
						n *= 8;

						if (n >= 0 && n <= 55008) {
							if (draw)
								drawChar2(n, x, tmp, colour);
							x += 9;
						} else {
							if (draw)
								drawChar('_', x, y, colour);
							x += 8;
						}
					} else {  //elisa font is not available
						ascii = 0xFF;
						if (code >> 8 == 0x81)
							ascii = sjis_lookup_81[code & 0x00FF];
						else if (code >> 8 == 0x82)
							ascii = sjis_lookup_82[code & 0x00FF];
						if (ascii != 0xFF) {
							if (draw)
								drawChar((char)ascii, x, y, colour);
						} else {
							if (draw)
								drawChar('_', x, y, colour);
						}
						x += 8;
					}
					break;
			}
		} else {  //First char does not have top bit set or no following char
			if (draw)
				drawChar(s[i], x, y, colour);
			i++;
			x += 8;
		}
		if (x > SCREEN_WIDTH - SCREEN_MARGIN - FONT_WIDTH) {
			//x=16; y=y+8;
			return x;
		}
	}
	return x;
}
char *transcpy_sjis(char *d, const unsigned char *s)
{
	u8 ascii;
	u16 code1, code2;
	int i, j;

	for (i = 0, j = 0; s[i];) {
		code1 = s[i++];
		if ((code1 & 0x80) && s[i]) {  //we have top bit and some more char (SJIS) ?
			// SJIS
			code2 = s[i++];
			ascii = 0xFF;
			if (code1 == 0x81)
				ascii = sjis_lookup_81[code2];
			else if (code1 == 0x82)
				ascii = sjis_lookup_82[code2];
			if (ascii != 0xFF) {
				d[j++] = (char)ascii;
			} else {
				d[j++] = '_';
			}
		} else {  //First char lacks top bit set or no following char (non-SJIS)
			d[j++] = (char)code1;
		}
	}             //ends for
	d[j] = '\0';  //terminate result string
	return d;
}
//--------------------------------------------------------------
//End of file: draw_text.c
//--------------------------------------------------------------
