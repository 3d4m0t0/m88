// ---------------------------------------------------------------------------
//	Standalone Z80 engine test harness for Linux / cross-build validation.
// ---------------------------------------------------------------------------

#include "headers.h"
#include "device.h"
#include "Z80Test.h"
#include "Z80C.h"

#include <cstdlib>
#include <ctime>

namespace {

uint8 host_ram[0x10000];

void FillRandomMemory(uint seed)
{
	srand(seed);
	for (unsigned i = 0; i < 0x10000; ++i)
		host_ram[i] = static_cast<uint8>(rand() & 0xff);

	// Keep reset vector in ROM-like low page: JP 0x0100
	host_ram[0x0000] = 0xc3;
	host_ram[0x0001] = 0x00;
	host_ram[0x0002] = 0x01;
	// HALT trap after a short run region
	host_ram[0x01fe] = 0x76;
	host_ram[0x01ff] = 0x76;
}

int RunInstructionCompare(unsigned seed, int steps)
{
#if !Z80TEST_HAS_X86
	fprintf(stderr, "z80test: instruction compare skipped (requires MSVC + Z80_x86)\n");
	return 0;
#else
	memset(host_ram, 0, sizeof(host_ram));
	FillRandomMemory(seed);

	Z80Test tester(DEV_ID('T', 'E', 'S', 'T'));
	if (!tester.Init(host_ram, 0))
	{
		fprintf(stderr, "z80test: Z80Test::Init failed\n");
		return 1;
	}

	fprintf(stderr, "z80test: comparing Z80C vs Z80_x86\n");

	tester.Reset();
	for (int i = 0; i < steps; ++i)
		tester.Exec(64);

	const uint errors = tester.GetErrorCount();
	if (errors)
		fprintf(stderr, "z80test: instruction compare failed (%u mismatches, seed=%u)\n", errors, seed);
	else
		fprintf(stderr, "z80test: instruction compare passed (seed=%u, steps=%d)\n", seed, steps);

	return errors ? 1 : 0;
#endif
}

class SyncPortDevice : public Device
{
public:
	SyncPortDevice()
	: Device(DEV_ID('S', 'Y', 'N', 'C')), port_fe_(0)
	{
	}

	void SetPort(uint8 value) { port_fe_ = value; }
	uint8 Port() const { return port_fe_; }

	uint IOCALL InFe(uint) { return port_fe_; }
	void IOCALL OutFe(uint, uint data) { port_fe_ = static_cast<uint8>(data); }

private:
	uint8 port_fe_;
};

struct DualCpuHarness
{
	uint8 ram[0x10000];
	MemoryManager mm;
	IOBus iobus;
	Z80C cpu;

	DualCpuHarness(const Device::ID& id)
	: cpu(id)
	{
	}

	bool Init(SyncPortDevice& pio)
	{
		MemoryPage *rd, *wr;
		if (!cpu.GetPages(&rd, &wr))
			return false;
		if (!mm.Init(0x10000, rd, wr))
			return false;
		if (!iobus.Init(0x120))
			return false;
		if (!cpu.Init(&mm, &iobus, 0))
			return false;

		const int pid = mm.Connect(nullptr);
		if (pid < 0)
			return false;
		if (!mm.AllocR(pid, 0, 0x10000, ram) || !mm.AllocW(pid, 0, 0x10000, ram))
			return false;

		iobus.ConnectIn(0xfe, &pio, STATIC_CAST(Device::InFuncPtr, &SyncPortDevice::InFe));
		iobus.ConnectOut(0xfe, &pio, STATIC_CAST(Device::OutFuncPtr, &SyncPortDevice::OutFe));
		iobus.GetFlags()[0xfe] = 1;
		return true;
	}
};

int RunDualSyncTest()
{
	SyncPortDevice pio;
	DualCpuHarness main_cpu(DEV_ID('M', 'A', 'I', 'N'));
	DualCpuHarness sub_cpu(DEV_ID('S', 'U', 'B', ' '));

	if (!main_cpu.Init(pio) || !sub_cpu.Init(pio))
	{
		fprintf(stderr, "z80test: dual-sync setup failed\n");
		return 1;
	}

	// Main: JP 0100h; LD A,08h; OUT (FEh),A; HALT
	main_cpu.ram[0x0000] = 0xc3;
	main_cpu.ram[0x0001] = 0x00;
	main_cpu.ram[0x0002] = 0x01;
	main_cpu.ram[0x0100] = 0x3e;
	main_cpu.ram[0x0101] = 0x08;
	main_cpu.ram[0x0102] = 0xd3;
	main_cpu.ram[0x0103] = 0xfe;
	main_cpu.ram[0x0104] = 0x76;

	// Sub: JP 00CEh; IN A,(FEh); AND 08h; JR Z,00CEh; HALT
	sub_cpu.ram[0x0000] = 0xc3;
	sub_cpu.ram[0x0001] = 0xce;
	sub_cpu.ram[0x0002] = 0x00;
	sub_cpu.ram[0x00ce] = 0xdb;
	sub_cpu.ram[0x00cf] = 0xfe;
	sub_cpu.ram[0x00d0] = 0xe6;
	sub_cpu.ram[0x00d1] = 0x08;
	sub_cpu.ram[0x00d2] = 0x28;
	sub_cpu.ram[0x00d3] = 0xfa;
	sub_cpu.ram[0x00d4] = 0x76;

	main_cpu.cpu.Reset();
	sub_cpu.cpu.Reset();

	const int ran = Z80C::ExecDual(&main_cpu.cpu, &sub_cpu.cpu, 20000);
	const uint sub_pc = sub_cpu.cpu.GetPC();
	const uint main_pc = main_cpu.cpu.GetPC();

	fprintf(stderr, "z80test: dual-sync ran %d clocks, main PC=%.4x sub PC=%.4x port FE=%.2x\n",
		ran, main_pc, sub_pc, pio.Port());

	if (sub_pc != 0x00d4 && sub_pc != 0x00d5)
	{
		fprintf(stderr, "z80test: dual-sync failed (sub CPU did not clear FDIF-style wait)\n");
		return 1;
	}

	fprintf(stderr, "z80test: dual-sync passed\n");
	return 0;
}

} // namespace

int main(int argc, char** argv)
{
	unsigned seed = 1;
	int steps = 4096;
	if (argc > 1)
		seed = static_cast<unsigned>(strtoul(argv[1], nullptr, 0));
	if (argc > 2)
		steps = atoi(argv[2]);

	int failures = 0;
	failures += RunInstructionCompare(seed, steps);
	failures += RunDualSyncTest();

	return failures ? 1 : 0;
}
