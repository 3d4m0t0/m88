// ----------------------------------------------------------------------------
//	M88 - PC-88 emulator
//	Copyright (C) cisc 1998.
// ----------------------------------------------------------------------------
//	Z80 ?G?~?????[?V?????p?b?P?[?W??r???s?p?N???X
//	$Id: Z80Test.cpp,v 1.6 1999/08/14 14:45:06 cisc Exp $

#include "headers.h"
#include "Z80Test.h"
#include "device_i.h"

Z80Test* Z80Test::currentcpu=0;

// ----------------------------------------------------------------------------

Z80Test::Z80Test(const ID& id)
: Device(id), cpu1('cpur'), cpu2('cpur'), mm1pid(-1), mm2pid(-1), error_count(0)
{
	char buf[] = "    .dmp";
	buf[0] = (id >> 24) & 0xff; buf[1] = (id >> 16) & 0xff;
	buf[2] = (id >>  8) & 0xff; buf[3] = (id      ) & 0xff;
	
	fp = fopen(buf, "w");
}

Z80Test::~Z80Test()
{
	if (fp)
		fclose(fp);
}

// ----------------------------------------------------------------------------

bool Z80Test::Init(uint8* host_ram_, int iack)
{
	host_ram = host_ram_;

	MemoryPage *rd1, *wr1, *rd2, *wr2;
	if (!cpu1.GetPages(&rd1, &wr1) || !cpu2.GetPages(&rd2, &wr2))
		return false;
	if (!mm1.Init(0x10000, rd1, wr1) || !mm2.Init(0x10000, rd2, wr2))
		return false;
	if (!iobus1.Init(0x120) || !iobus2.Init(0x120))
		return false;
	if (!cpu1.Init(&mm1, &iobus1, iack) || !cpu2.Init(&mm2, &iobus2, iack))
		return false;

	mm1pid = mm1.Connect(this);
	mm2pid = mm2.Connect(this);
	if (mm1pid < 0 || mm2pid < 0)
		return false;
	if (!mm1.AllocR(mm1pid, 0, 0x10000, S_Read8R) || !mm1.AllocW(mm1pid, 0, 0x10000, S_Write8R))
		return false;
	if (!mm2.AllocR(mm2pid, 0, 0x10000, S_Read8T) || !mm2.AllocW(mm2pid, 0, 0x10000, S_Write8T))
		return false;

	for (int p=0; p<0x120; p++)
	{
		iobus1.ConnectIn (p, this, STATIC_CAST(Device::InFuncPtr, &Z80Test::InR));
		iobus1.ConnectOut(p, this, STATIC_CAST(Device::OutFuncPtr, &Z80Test::OutR));
		iobus2.ConnectIn (p, this, STATIC_CAST(Device::InFuncPtr, &Z80Test::InT));
		iobus2.ConnectOut(p, this, STATIC_CAST(Device::OutFuncPtr, &Z80Test::OutT));
	}

	execcount = 0;
	error_count = 0;

	return true;
}

// ----------------------------------------------------------------------------

int Z80Test::ExecDual(Z80Test* first, Z80Test* second, int nclocks)
{
	currentcpu = first;
	first->execcount += first->clockcount;
	second->execcount += second->clockcount;
	first->clockcount = -nclocks;
	second->clockcount = 0;

	first->readcount = first->writecount = first->readcountt = first->writecountt = first->codesize = 0;
	first->cpu1.TestIntr();	first->cpu2.TestIntr();
	second->readcount = second->writecount = second->readcountt = second->writecountt = second->codesize = 0;
	second->cpu1.TestIntr(); second->cpu2.TestIntr();

	while (first->clockcount < 0)
	{
		first->Test();
		second->Test();
	}
		
	return nclocks + first->clockcount;
}

// ----------------------------------------------------------------------------

int Z80Test::Exec(int step)
{
	execcount += clockcount;
	clockcount = -step;

	readcount = writecount = readcountt = writecountt = codesize = 0;
	cpu1.TestIntr();
	cpu2.TestIntr();

	while (clockcount < 0)
		Test();

	return step + clockcount;
}

// ---------------------------------------------------------------------------
//	Exec ??r??????f
//
void Z80Test::Stop(int count)
{
	execcount += clockcount + count;
	clockcount = -count;
}

// ----------------------------------------------------------------------------

void Z80Test::Test()
{
	Z80Reg reg1 = cpu1.GetReg();
	Z80Reg reg2 = cpu2.GetReg();

	// ??????
	readcount = writecount = readcountt = writecountt = codesize = 0;
	reg = reg2 = reg1;					// ???W?X?^????v??????

	pc = reg1.pc;
	cpu2.SetPC(pc);
	intr = cpu1.IsIntr() + 2 * cpu2.IsIntr();

	// ???s
	clockcount += cpu1.ExecOne();
	cpu2.ExecOne();

	// ?t???O???v???m?F
	if ((reg1.r.b.flags & 0xd7) != (reg2.r.b.flags & 0xd7))
	{
		Error("?t???O??s??v");
		return;
	}

	// ???W?X?^???v?m?F
	if (((reg1.r.b.a ^ reg2.r.b.a)
		| (reg1.r.w.bc ^ reg2.r.w.bc)
		| (reg1.r.w.de ^ reg2.r.w.de)
		| (reg1.r.w.hl ^ reg2.r.w.hl)
		| (reg1.r.w.ix ^ reg2.r.w.ix)
		| (reg1.r.w.iy ^ reg2.r.w.iy)
		| (reg1.r.w.sp ^ reg2.r.w.sp)
		| (!reg1.iff1 ^ !reg2.iff1)
		) & 0xffff)
	{
		Error("???W?X?^??s??v");
		return;
	}

	if (readcount != readcountt)
	{
		Error("?????????s??v");
		return;
	}

	if (writecount != writecountt)
	{
		Error("???????????s??v");
		return;
	}

	// PC ???v?m?F
	if (cpu1.GetPC() != cpu2.GetPC())
	{
		Error("PC ??s??v");
		return;
	}
}

// ----------------------------------------------------------------------------

void Z80Test::Error(const char* errtxt)
{
	error_count++;
	fprintf(stderr, "Z80Test: %s at PC %.4x\n", errtxt, pc);
	if (fp)
	{
		if (code[0] == 0xfb)		// special case
			return;
		if (code[0] == 0xed && code[1] == 0xa2)
			return;
		uint i;
		fprintf(fp, "PC: %.4x   ", pc); 
		for (i=0; i<4; i++)
		{
			fprintf(fp, (i>codesize ? "   " : "%.2x "), code[i]);
		}

		Z80Reg reg1 = cpu1.GetReg();
		Z80Reg reg2 = cpu2.GetReg();

		fprintf(fp,	"%s  reads:ref=%d target=%d writes:ref=%d target=%d\n", errtxt, readcount, readcountt, writecount, writecountt);
		for (i=0; i<readcount; i++)
		{
			fprintf(fp, "Read %d: %.4x %.2x\n", i, readptr[i], readdat[i]);
		}
		for (i=0; i<writecount; i++)
		{
			fprintf(fp, "Write %d: %.4x %.2x\n", i, writeptr[i], writedat[i]);
		}

		fprintf(fp, "     O PC:%.4x SP:%.4x AF:%.4x HL:%.4x DE:%.4x BC:%.4x IX:%.4x IY:%.4x IFF%d IRQ:%d R:%.2x\n",
					pc, reg.r.w.sp & 0xffff, reg.r.w.af & 0xffd7, reg.r.w.hl & 0xffff,
					reg.r.w.de & 0xffff, reg.r.w.bc & 0xffff, reg.r.w.ix & 0xffff, reg.r.w.iy & 0xffff, reg.iff1, intr, reg.rreg);
		
		fprintf(fp, "     O PC:%.4x SP:%.4x AF:%.4x HL:%.4x DE:%.4x BC:%.4x IX:%.4x IY:%.4x IFF%d IRQ:%d IM:%d R:%.2x\n",
					cpu1.GetPC(), reg1.r.w.sp & 0xffff, reg1.r.w.af & 0xffd7, reg1.r.w.hl & 0xffff,
					reg1.r.w.de & 0xffff, reg1.r.w.bc & 0xffff, reg1.r.w.ix & 0xffff, reg1.r.w.iy & 0xffff, reg1.iff1, cpu1.IsIntr(), reg1.intmode, reg1.rreg);
		
		fprintf(fp, "     O PC:%.4x SP:%.4x AF:%.4x HL:%.4x DE:%.4x BC:%.4x IX:%.4x IY:%.4x IFF%d IRQ:%d IM:%d R:%.2x\n",
					cpu2.GetPC(), reg2.r.w.sp & 0xffff, reg2.r.w.af & 0xffd7, reg2.r.w.hl & 0xffff,
					reg2.r.w.de & 0xffff, reg2.r.w.bc & 0xffff, reg2.r.w.ix & 0xffff, reg2.r.w.iy & 0xffff, reg2.iff1, cpu2.IsIntr(), reg2.intmode, reg2.rreg);
	
	}
}

// ----------------------------------------------------------------------------

inline uint Z80Test::Read8R(uint a)
{
	uint fcount = a - pc;
	if (fcount < 4)
	{
		codesize = (fcount > codesize) ? fcount : codesize;
		return code[fcount] = host_ram[a];
	}

	if (readcount < 8)
	{
		uint data = host_ram[a];
		readptr[readcount] = a;
		readdat[readcount] = data;
		readcount++;
		return data;
	}
	fprintf(fp, "%x %x\n", a, pc);
	Error("?P???????W?o?C?g??????f?[?^???????");
	return host_ram[a];
}

// ----------------------------------------------------------------------------

inline uint Z80Test::Read8T(uint a)
{
	uint fcount = a - pc;
	if (fcount < 4)
		return code[fcount];
	
	readcountt++;
	for (uint i=0; i<readcount; i++)
	{
		if (((readptr[i] ^ a) & 0xffff) == 0)
			return readdat[i];
	}

	char buf[128];
	sprintf(buf, "??????A?h???X??s??v: %.4x", a);
	Error(buf);
	return 0;
}

// ----------------------------------------------------------------------------

inline void Z80Test::Write8R(uint a, uint d)
{
	d &= 0xff;
	if (writecount < 8)
	{
		writeptr[writecount] = a;
		writedat[writecount] = d;
		writecount ++;
		host_ram[a] = static_cast<uint8>(d);
		return;
	}
	Error("?P???????W?o?C?g??????f?[?^?????????");
}

inline void Z80Test::Write8T(uint a, uint d)
{
	d &= 0xff;
	writecountt++;
	for (uint i=0; i<writecount; i++)
	{
		if (((writeptr[i] ^ a) & 0xffff) == 0)
		{
			if (writedat[i] != d)
			{
				char buf[128];
				sprintf(buf, "????????f?[?^??s??v at %.4x:%.2x %.2x", a, writedat[i], d);
				Error(buf);
			}
			return;
		}
	}
	char buf[128];
	sprintf(buf, "????????A?h???X??s??v: %.4x", a);
	Error(buf);
}

// ----------------------------------------------------------------------------

uint Z80Test::InR(uint a)
{
	uint data = 0xff;
	inptr = a, indat = data;
	return data;
}

uint Z80Test::InT(uint a)
{
	if (inptr == a)
		return indat;
	Error("????|?[?g??s??v");
	return 0;
}

// ----------------------------------------------------------------------------

void Z80Test::OutR(uint a, uint d)
{
	outptr = a, outdat = d;
}

void Z80Test::OutT(uint a, uint d)
{
	if (outptr == a)
	{
		if (outdat != d)
			Error("?o??f?[?^??s??v");
		return;
	}
	Error("?o??|?[?g??s??v");
}

// ----------------------------------------------------------------------------

void Z80Test::Reset(uint, uint)
{
	cpu1.Reset();
	cpu2.Reset();
}

void Z80Test::IRQ(uint, uint d)
{
	cpu1.IRQ(0, d);
	cpu2.IRQ(0, d);
}

void Z80Test::NMI(uint, uint)
{
}

void Z80Test::Wait(bool)
{
}

// ----------------------------------------------------------------------------

uint MEMCALL Z80Test::S_Read8R(void* inst, uint a)
{
	return ((Z80Test*)(inst))->Read8R(a);
}

uint MEMCALL Z80Test::S_Read8T(void* inst, uint a)
{
	return ((Z80Test*)(inst))->Read8T(a);
}

void MEMCALL Z80Test::S_Write8R(void* inst, uint a, uint d)
{
	((Z80Test*)(inst))->Write8R(a, d);
}

void MEMCALL Z80Test::S_Write8T(void* inst, uint a, uint d)
{
	((Z80Test*)(inst))->Write8T(a, d);
}

// ---------------------------------------------------------------------------
//	Device descriptor
//	
const Device::Descriptor Z80Test::descriptor =
{
	0, outdef
};

const Device::OutFuncPtr Z80Test::outdef[] =
{
	STATIC_CAST(Device::OutFuncPtr, &Z80Test::Reset),
	STATIC_CAST(Device::OutFuncPtr, &Z80Test::IRQ),
	STATIC_CAST(Device::OutFuncPtr, &Z80Test::NMI),
};
