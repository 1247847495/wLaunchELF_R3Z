#!/bin/sh
#ps2dev/ps2dev:latest is Alpine based (no bash); detect the toolchain
#prefix used by the image (newer images: /usr/local/ps2dev).
if [ -d /usr/local/ps2dev/ee/bin ]; then
  export PS2DEV=/usr/local/ps2dev
else
  export PS2DEV=/root/.local/ps2dev
fi
export PS2SDK=$PS2DEV/ps2sdk
export PATH=$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2DEV/bin:$PS2SDK/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
#ps2dev/ps2dev:latest ships the toolchain only; add build tools when missing.
if ! command -v make >/dev/null 2>&1 || ! command -v git >/dev/null 2>&1; then
  apk add --no-cache make git >/dev/null 2>&1 || exit 1
fi
cd /mnt/d/wLaunchELF_R3Z-master || exit 1

#Single ELF with both network features: SMB and UDPFS each bind SMAP
#with their own stack, so the app switches between them at runtime via
#an IOP reset (see switchNetworkStack in src/init.c).
LOG=/tmp/wle_build.log
#vmcman/mmceman 必须本地编译,其Makefile依赖 ps2sdk 源码树中的
#iop/Rules.bin.make 等文件(安装版SDK没有),因此容器内浅克隆一份并导出PS2SDKSRC。
if [ ! -f /tmp/ps2sdk/iop/Rules.bin.make ]; then
  git clone --depth 1 https://github.com/ps2dev/ps2sdk /tmp/ps2sdk >> "$LOG" 2>&1 || exit 1
fi
export PS2SDKSRC=/tmp/ps2sdk
#安装版SDK自带预编译的srxfixup。上游 iop/Rules.make 硬编码从
#$(PS2SDKSRC)/tools/srxfixup/bin/srxfixup 取该工具并会尝试用主机cc从源码编译
#(Alpine容器没有cc),因此直接把预编译版放到该路径并touch成最新,让make视为已构建。
mkdir -p /tmp/ps2sdk/tools/srxfixup/bin
cp "$PS2SDK/bin/srxfixup" /tmp/ps2sdk/tools/srxfixup/bin/srxfixup || exit 1
touch /tmp/ps2sdk/tools/srxfixup/bin/srxfixup
#全量清理:连本地编译的IOP驱动产物(iop/*.irx、iop/__generated、iop子目录)一起清掉,
#彻底排除陈旧/损坏的驱动被重复嵌入的可能。
make clean > "$LOG" 2>&1
rm -rf obj asm githash.h UNC-BOOT-*.ELF BOOT-*.ELF
make githash.h >> "$LOG" 2>&1 || exit 1
#串行编译,排除并行构建引入的非确定性
make -j1 SMB=1 UDPFS=1 >> "$LOG" 2>&1
ret=$?
echo "=== BUILD EXIT: $ret ==="
tail -20 "$LOG"
if [ $ret -ne 0 ]; then
  echo "=== ERROR LINES ==="
  grep -n -iE "error:|undefined reference|No such file" "$LOG" | tail -40
fi
ls -l UNC-BOOT-*.ELF BOOT-*.ELF
exit $ret
